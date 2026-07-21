/**
 * bitmap.js
 *
 * Bitmap editor for the Waveshare 3.52" (black/white/red) e-paper
 * panel. Renders a native-resolution canvas, lets the user paint with
 * a 3-color palette, then packs the result into the exact wire format
 * the ESP8266 display-service expects (a 12-byte header followed by
 * two 1bpp MSB-first bitplanes) and uploads it as multipart/form-data
 * so the device can stream it straight into its framebuffer without
 * ever buffering the whole body on the heap.
 *
 * Wire format (must match src/display-service/display_protocol.h):
 *   offset 0  uint16 magic      (0x4550, little-endian)
 *   offset 2  uint8  version    (1)
 *   offset 3  uint8  encoding   (0 = Packed2Bit)
 *   offset 4  uint16 width      (little-endian)
 *   offset 6  uint16 height     (little-endian)
 *   offset 8  uint32 crc32      (little-endian, over black+red planes only)
 *   offset 12 black plane (rowBytes * height bytes)
 *   offset 12+planeSize red plane (rowBytes * height bytes)
 */
(function () {
    "use strict";

    const DISPLAY_BITMAP_MAGIC = 0x4550;
    const DISPLAY_BITMAP_VERSION = 1;
    const BITMAP_ENCODING_PACKED_2BIT = 0;
    const HEADER_SIZE = 12;

    const COLOR_WHITE = 0;
    const COLOR_BLACK = 1;
    const COLOR_RED = 2;

    const PALETTE = {
        [COLOR_WHITE]: "#ffffff",
        [COLOR_BLACK]: "#000000",
        [COLOR_RED]: "#cc2222",
    };

    /** Default panel size; overwritten by /api/display/status once it responds. */
    let displayWidth = 240;
    let displayHeight = 360;
    let rowBytes = (displayWidth + 7) >> 3;
    let planeSize = rowBytes * displayHeight;

    /** One byte per pixel: COLOR_WHITE / COLOR_BLACK / COLOR_RED. */
    let pixels = new Uint8Array(displayWidth * displayHeight);

    let canvas = null;
    let ctx = null;
    let activeColor = COLOR_BLACK;
    let isPainting = false;
    let statusEl = null;

    /**
     * Compute the running CRC32 (IEEE 802.3 polynomial), matching the
     * bit-by-bit implementation used on the device.
     *
     * @param {Uint8Array} bytes
     * @returns {number} Unsigned 32-bit CRC32.
     */
    function crc32(bytes) {
        let crc = 0xffffffff;
        for (let i = 0; i < bytes.length; i++) {
            crc ^= bytes[i];
            for (let bit = 0; bit < 8; bit++) {
                const mask = -(crc & 1);
                crc = (crc >>> 1) ^ (0xedb88320 & mask);
            }
        }
        return (crc ^ 0xffffffff) >>> 0;
    }

    /**
     * Resize the pixel model (and canvas) to a new panel resolution.
     *
     * @param {number} width
     * @param {number} height
     */
    function resize(width, height) {
        displayWidth = width;
        displayHeight = height;
        rowBytes = (displayWidth + 7) >> 3;
        planeSize = rowBytes * displayHeight;
        pixels = new Uint8Array(displayWidth * displayHeight);

        canvas.width = displayWidth;
        canvas.height = displayHeight;
        render();
    }

    /** Redraw the full canvas from the pixel model. */
    function render() {
        const image = ctx.createImageData(displayWidth, displayHeight);
        for (let i = 0; i < pixels.length; i++) {
            const hex = PALETTE[pixels[i]];
            const r = parseInt(hex.substr(1, 2), 16);
            const g = parseInt(hex.substr(3, 2), 16);
            const b = parseInt(hex.substr(5, 2), 16);
            const o = i * 4;
            image.data[o] = r;
            image.data[o + 1] = g;
            image.data[o + 2] = b;
            image.data[o + 3] = 255;
        }
        ctx.putImageData(image, 0, 0);
    }

    /**
     * Paint a single pixel at canvas-space coordinates (clamped to bounds).
     *
     * @param {number} x
     * @param {number} y
     */
    function paintAt(x, y) {
        if (x < 0 || y < 0 || x >= displayWidth || y >= displayHeight) return;
        const index = y * displayWidth + x;
        if (pixels[index] === activeColor) return;
        pixels[index] = activeColor;

        const hex = PALETTE[activeColor];
        ctx.fillStyle = hex;
        ctx.fillRect(x, y, 1, 1);
    }

    /**
     * Translate a mouse/touch client position into canvas pixel coordinates.
     *
     * @param {number} clientX
     * @param {number} clientY
     * @returns {{x: number, y: number}}
     */
    function toCanvasCoords(clientX, clientY) {
        const rect = canvas.getBoundingClientRect();
        const scaleX = displayWidth / rect.width;
        const scaleY = displayHeight / rect.height;
        return {
            x: Math.floor((clientX - rect.left) * scaleX),
            y: Math.floor((clientY - rect.top) * scaleY),
        };
    }

    /**
     * Pack the current pixel model into the device wire format.
     *
     * @returns {Uint8Array} header + black plane + red plane.
     */
    function packFrame() {
        const blackPlane = new Uint8Array(planeSize).fill(0xff);
        const redPlane = new Uint8Array(planeSize).fill(0xff);

        for (let y = 0; y < displayHeight; y++) {
            for (let x = 0; x < displayWidth; x++) {
                const color = pixels[y * displayWidth + x];
                if (color === COLOR_WHITE) continue;

                const byteIndex = (x >> 3) + y * rowBytes;
                const bitMask = 0x80 >> (x & 7);
                if (color === COLOR_BLACK) {
                    blackPlane[byteIndex] &= ~bitMask;
                } else if (color === COLOR_RED) {
                    redPlane[byteIndex] &= ~bitMask;
                }
            }
        }

        const planes = new Uint8Array(planeSize * 2);
        planes.set(blackPlane, 0);
        planes.set(redPlane, planeSize);

        const frame = new Uint8Array(HEADER_SIZE + planes.length);
        const view = new DataView(frame.buffer);
        view.setUint16(0, DISPLAY_BITMAP_MAGIC, true);
        view.setUint8(2, DISPLAY_BITMAP_VERSION);
        view.setUint8(3, BITMAP_ENCODING_PACKED_2BIT);
        view.setUint16(4, displayWidth, true);
        view.setUint16(6, displayHeight, true);
        view.setUint32(8, crc32(planes), true);
        frame.set(planes, HEADER_SIZE);

        return frame;
    }

    /** Upload the current pixel model to the device and trigger a refresh. */
    async function sendFrame() {
        setStatus("Sending...");
        try {
            const frame = packFrame();
            const formData = new FormData();
            formData.append("frame", new Blob([frame]), "frame.bin");

            const response = await fetch("/api/display/frame", {
                method: "POST",
                body: formData,
            });
            const body = await response.json().catch(() => ({}));

            if (response.ok) {
                setStatus(body.displayed === false
                    ? "Frame accepted, but the display hardware isn't available."
                    : "Sent to display.");
            } else {
                setStatus("Error: " + (body.message || response.statusText));
            }
        } catch (err) {
            setStatus("Error: " + err.message);
        }
    }

    /**
     * @param {string} message
     */
    function setStatus(message) {
        if (statusEl) statusEl.textContent = message;
    }

    /** Reset the pixel model to all-white. */
    function clearCanvas() {
        pixels.fill(COLOR_WHITE);
        render();
    }

    /** Wire up palette buttons, canvas painting, and action buttons. */
    function bindControls() {
        document.querySelectorAll("[data-bitmap-color]").forEach((button) => {
            button.addEventListener("click", () => {
                activeColor = parseInt(button.getAttribute("data-bitmap-color"), 10);
                document.querySelectorAll("[data-bitmap-color]").forEach((b) => b.classList.remove("active"));
                button.classList.add("active");
            });
        });

        const clearButton = document.getElementById("bitmap-clear");
        if (clearButton) clearButton.addEventListener("click", clearCanvas);

        const sendButton = document.getElementById("bitmap-send");
        if (sendButton) sendButton.addEventListener("click", sendFrame);

        const paintFromEvent = (event) => {
            const point = event.touches ? event.touches[0] : event;
            const { x, y } = toCanvasCoords(point.clientX, point.clientY);
            paintAt(x, y);
        };

        canvas.addEventListener("mousedown", (event) => {
            isPainting = true;
            paintFromEvent(event);
        });
        canvas.addEventListener("mousemove", (event) => {
            if (isPainting) paintFromEvent(event);
        });
        window.addEventListener("mouseup", () => {
            isPainting = false;
        });

        canvas.addEventListener("touchstart", (event) => {
            isPainting = true;
            paintFromEvent(event);
            event.preventDefault();
        });
        canvas.addEventListener("touchmove", (event) => {
            if (isPainting) paintFromEvent(event);
            event.preventDefault();
        });
        canvas.addEventListener("touchend", () => {
            isPainting = false;
        });
    }

    /** Fetch the panel's real dimensions and size the canvas to match. */
    async function loadDisplayStatus() {
        try {
            const response = await fetch("/api/display/status");
            const body = await response.json();
            if (body.width && body.height) {
                resize(body.width, body.height);
            }
        } catch (err) {
            // Fall back to the compiled-in default size.
        }
    }

    function init() {
        canvas = document.getElementById("bitmap-canvas");
        if (!canvas) return;
        ctx = canvas.getContext("2d");
        statusEl = document.getElementById("bitmap-status");

        canvas.width = displayWidth;
        canvas.height = displayHeight;
        render();
        bindControls();
        loadDisplayStatus();
    }

    if (document.readyState === "loading") {
        document.addEventListener("DOMContentLoaded", init);
    } else {
        init();
    }
})();