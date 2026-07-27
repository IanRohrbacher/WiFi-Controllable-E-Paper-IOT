/**
 * bitmap.js
 *
 * Bitmap editor for the Waveshare 3.52" (black/white/red) e-paper panel.
 * Renders a canvas, lets the user paint with a 3-color palette, then packs the
 * result into the exact wire format the display module expects (a 12-byte
 * header followed by two 1bpp MSB-first bitplanes) and uploads it as
 * multipart/form-data so the device can stream it straight into its
 * framebuffer without ever buffering the whole body on the heap.
 *
 * Wire format (must match `src/display/display_protocol.h`):
 *   offset 0  uint16 magic      (0x4550, little-endian)
 *   offset 2  uint8  version    (1)
 *   offset 3  uint8  encoding   (0 = Packed2Bit)
 *   offset 4  uint16 width      (little-endian, device-native, never rotated)
 *   offset 6  uint16 height     (little-endian, device-native, never rotated)
 *   offset 8  uint32 crc32      (little-endian, over black+red planes only)
 *   offset 12 black plane (rowBytes * height bytes)
 *   offset 12+planeSize red plane (rowBytes * height bytes)
 *
 * Rotation: the firmware never rotates anything, it always expects bytes in
 * the panel's native scan order (see `/api/display/status`'s "width"/
 * "height", which are always device-native regardless of rotation). All
 * rotation happens here: the editing canvas is sized (and possibly
 * width/height-swapped for 90/270) according to `/api/display/status`'s
 * "rotation" field, and `packFrame()` maps each canvas pixel to its rotated
 * position in the device-native buffer before packing bits.
 * 
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

    /** Device-native panel size and rotation; overwritten by /api/display/status. */
    let deviceWidth = 240;
    let deviceHeight = 360;
    let rotationDegrees = 0;
    let rowBytes = (deviceWidth + 7) >> 3;
    let planeSize = rowBytes * deviceHeight;

    /** Editing canvas size: same as device size, swapped for 90/270 rotation. */
    let canvasWidth = deviceWidth;
    let canvasHeight = deviceHeight;

    /** One byte per pixel: COLOR_WHITE / COLOR_BLACK / COLOR_RED. */
    let pixels = new Uint8Array(canvasWidth * canvasHeight);

    let canvas = null;
    let ctx = null;
    let activeColor = COLOR_BLACK;
    let isPainting = false;
    let statusEl = null;

    /**
     * Compute the running CRC32 (IEEE 802.3 polynomial), matching the
     * bit-by-bit implementation used on the device (see `crc32Update()` in
     * `src/display/display.cpp`).
     *
     * @param {Uint8Array} bytes
     * @returns {number} Unsigned 32-bit CRC32.
     * 
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
     * Map a canvas (editing) pixel to its position in the device-native
     * buffer, according to the configured rotation.
     *
     * @param {number} cx Canvas-space x, in [0, canvasWidth).
     * @param {number} cy Canvas-space y, in [0, canvasHeight).
     * @returns {{dx: number, dy: number}} Device-space position.
     * 
     */
    function toDevicePixel(cx, cy) {
        switch (rotationDegrees) {
            case 90:
                return { dx: cy, dy: cx };
            case 180:
                return { dx: deviceWidth - 1 - cx, dy: deviceHeight - 1 - cy };
            case 270:
                return { dx: deviceWidth - 1 - cy, dy: deviceHeight - 1 - cx };
            case 0:
            default:
                return { dx: cx, dy: cy };
        }
    }

    /**
     * Resize the pixel model (and canvas) for a new panel resolution/rotation.
     *
     * @param {number} width Device-native width.
     * @param {number} height Device-native height.
     * @param {number} rotation 0, 90, 180, or 270.
     * 
     */
    function resize(width, height, rotation) {
        deviceWidth = width;
        deviceHeight = height;
        rotationDegrees = rotation;
        rowBytes = (deviceWidth + 7) >> 3;
        planeSize = rowBytes * deviceHeight;

        const swapped = rotationDegrees === 90 || rotationDegrees === 270;
        canvasWidth = swapped ? deviceHeight : deviceWidth;
        canvasHeight = swapped ? deviceWidth : deviceHeight;
        pixels = new Uint8Array(canvasWidth * canvasHeight);

        canvas.width = canvasWidth;
        canvas.height = canvasHeight;
        render();
    }

    /** Redraw the full canvas from the pixel model. */
    function render() {
        const image = ctx.createImageData(canvasWidth, canvasHeight);
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
     * 
     */
    function paintAt(x, y) {
        if (x < 0 || y < 0 || x >= canvasWidth || y >= canvasHeight) return;
        const index = y * canvasWidth + x;
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
     * 
     */
    function toCanvasCoords(clientX, clientY) {
        const rect = canvas.getBoundingClientRect();
        const scaleX = canvasWidth / rect.width;
        const scaleY = canvasHeight / rect.height;
        return {
            x: Math.floor((clientX - rect.left) * scaleX),
            y: Math.floor((clientY - rect.top) * scaleY),
        };
    }

    /**
     * Pack the current pixel model into the device wire format, rotating
     * each pixel into its device-native position along the way.
     *
     * @returns {Uint8Array} header + black plane + red plane.
     * @see toDevicePixel
     * 
     */
    function packFrame() {
        const blackPlane = new Uint8Array(planeSize).fill(0xff);
        const redPlane = new Uint8Array(planeSize).fill(0xff);

        for (let cy = 0; cy < canvasHeight; cy++) {
            for (let cx = 0; cx < canvasWidth; cx++) {
                const color = pixels[cy * canvasWidth + cx];
                if (color === COLOR_WHITE) continue;

                const { dx, dy } = toDevicePixel(cx, cy);
                const byteIndex = (dx >> 3) + dy * rowBytes;
                const bitMask = 0x80 >> (dx & 7);
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
        view.setUint16(4, deviceWidth, true);
        view.setUint16(6, deviceHeight, true);
        view.setUint32(8, crc32(planes), true);
        frame.set(planes, HEADER_SIZE);

        return frame;
    }

    /** Upload the current pixel model to `/api/display/frame`. The server refreshes the panel on success. */
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
     * Show a status message to the user.
     *
     * @param {string} message
     * 
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

    /** Fetch the panel's real dimensions/rotation from `/api/display/status` and size the canvas to match. */
    async function loadDisplayStatus() {
        try {
            const response = await fetch("/api/display/status", { cache: "no-store" });
            const body = await response.json();
            if (body.width && body.height) {
                resize(body.width, body.height, body.rotation || 0);
            }
        } catch (err) {
            // Fall back to the compiled-in default size/rotation.
        }
    }

    /** Wire up the canvas/context/status elements and kick off rendering. */
    function init() {
        canvas = document.getElementById("bitmap-canvas");
        if (!canvas) return;
        ctx = canvas.getContext("2d");
        statusEl = document.getElementById("bitmap-status");

        canvas.width = canvasWidth;
        canvas.height = canvasHeight;
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
