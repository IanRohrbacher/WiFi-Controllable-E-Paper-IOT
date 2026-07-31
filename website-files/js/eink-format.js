/**
 * eink-format.js
 *
 * Stateless wire-format helpers shared by `bitmap-editor.js` (the interactive
 * canvas on `index.html`) and `blocked-preview.js` (the read-only saved-canvas
 * preview on `blocked.html`). Every function here takes its canvas/device
 * dimensions and rotation as explicit arguments rather than closing over any
 * module-level state, so both entry points can reuse the exact same pack,
 * unpack, render, and autosave logic without duplicating it.
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
 * rotation happens client side: a canvas is sized (and possibly
 * width/height-swapped for 90/270) according to `/api/display/status`'s
 * "rotation" field, and `packFrame()`/`unpackFrameToPixels()` map between
 * canvas-space and the rotated device-native buffer.
 *
 */

export const DISPLAY_BITMAP_MAGIC = 0x4550;
export const DISPLAY_BITMAP_VERSION = 1;
export const BITMAP_ENCODING_PACKED_2BIT = 0;
export const HEADER_SIZE = 12;

export const COLOR_WHITE = 0;
export const COLOR_BLACK = 1;
export const COLOR_RED = 2;

export const PALETTE = {
    [COLOR_WHITE]: "#ffffff",
    [COLOR_BLACK]: "#000000",
    [COLOR_RED]: "#cc2222",
};

/** Extension (and upload `accept` filter) used for saved canvas files. Change this one constant to rename the format. */
export const EINK_FILE_EXTENSION = ".eink";

/** `localStorage` key the in-progress canvas is periodically autosaved under. */
export const AUTOSAVE_KEY = "einkCanvasAutosave";

/**
 * Compute the running CRC32 (IEEE 802.3 polynomial), matching the
 * bit-by-bit implementation used on the device (see `crc32Update()` in
 * `src/display/display.cpp`).
 *
 * @param {Uint8Array} bytes
 * @returns {number} Unsigned 32-bit CRC32.
 *
 */
export function crc32(bytes) {
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
 * Map a canvas-space pixel to its position in a device-native buffer.
 *
 * @param {number} cx Canvas-space x.
 * @param {number} cy Canvas-space y.
 * @param {number} deviceWidth Device-native width to map into.
 * @param {number} deviceHeight Device-native height to map into.
 * @param {number} rotation 0, 90, 180, or 270.
 * @returns {{dx: number, dy: number}} Device-space position.
 *
 */
export function toDevicePixel(cx, cy, deviceWidth, deviceHeight, rotation) {
    switch (rotation) {
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
 * Pack a canvas-space pixel model into the device wire format, rotating each
 * pixel into its device-native position along the way.
 *
 * @param {Uint8Array} pixels One byte per canvas pixel: COLOR_WHITE/BLACK/RED.
 * @param {number} canvasWidth
 * @param {number} canvasHeight
 * @param {number} rotation 0, 90, 180, or 270.
 * @returns {Uint8Array} header + black plane + red plane.
 * @see toDevicePixel
 *
 */
export function packFrame(pixels, canvasWidth, canvasHeight, rotation) {
    const swapped = rotation === 90 || rotation === 270;
    const deviceWidth = swapped ? canvasHeight : canvasWidth;
    const deviceHeight = swapped ? canvasWidth : canvasHeight;
    const rowBytes = (deviceWidth + 7) >> 3;
    const planeSize = rowBytes * deviceHeight;

    const blackPlane = new Uint8Array(planeSize).fill(0xff);
    const redPlane = new Uint8Array(planeSize).fill(0xff);

    for (let cy = 0; cy < canvasHeight; cy++) {
        for (let cx = 0; cx < canvasWidth; cx++) {
            const color = pixels[cy * canvasWidth + cx];
            if (color === COLOR_WHITE) continue;

            const { dx, dy } = toDevicePixel(cx, cy, deviceWidth, deviceHeight, rotation);
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

/**
 * Validate a `.eink` file's bytes and split it into its device-native planes.
 * Does not check width/height against any particular panel, nor unpack into
 * canvas space — callers do that with `unpackFrameToPixels()` once they've
 * confirmed the dimensions match the panel they care about.
 *
 * @param {Uint8Array} bytes Raw file contents.
 * @returns {{ok: true, width: number, height: number, blackPlane: Uint8Array, redPlane: Uint8Array}
 *          | {ok: false, reason: string}}
 *
 */
export function parseEinkFrame(bytes) {
    if (bytes.length < HEADER_SIZE) {
        return { ok: false, reason: `Not a valid ${EINK_FILE_EXTENSION} file: too short.` };
    }

    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const magic = view.getUint16(0, true);
    const version = view.getUint8(2);
    const encoding = view.getUint8(3);
    const width = view.getUint16(4, true);
    const height = view.getUint16(6, true);
    const headerCrc = view.getUint32(8, true);

    if (magic !== DISPLAY_BITMAP_MAGIC || version !== DISPLAY_BITMAP_VERSION || encoding !== BITMAP_ENCODING_PACKED_2BIT) {
        return { ok: false, reason: `Not a valid ${EINK_FILE_EXTENSION} file.` };
    }

    const rowBytes = (width + 7) >> 3;
    const planeSize = rowBytes * height;
    if (bytes.length !== HEADER_SIZE + 2 * planeSize) {
        return { ok: false, reason: `This ${EINK_FILE_EXTENSION} file has an unexpected size.` };
    }

    const planes = bytes.subarray(HEADER_SIZE);
    if (crc32(planes) !== headerCrc) {
        return { ok: false, reason: `This ${EINK_FILE_EXTENSION} file failed a checksum check.` };
    }

    return {
        ok: true,
        width,
        height,
        blackPlane: planes.subarray(0, planeSize),
        redPlane: planes.subarray(planeSize, planeSize * 2),
    };
}

/**
 * Inverse of `packFrame()`: expand a device-native black/red plane pair back
 * into a canvas-space pixel model.
 *
 * @param {Uint8Array} blackPlane
 * @param {Uint8Array} redPlane
 * @param {number} deviceWidth
 * @param {number} deviceHeight
 * @param {number} rotation 0, 90, 180, or 270.
 * @param {number} canvasWidth
 * @param {number} canvasHeight
 * @returns {Uint8Array} One byte per canvas pixel: COLOR_WHITE/BLACK/RED.
 *
 */
export function unpackFrameToPixels(blackPlane, redPlane, deviceWidth, deviceHeight, rotation, canvasWidth, canvasHeight) {
    const rowBytes = (deviceWidth + 7) >> 3;
    const pixels = new Uint8Array(canvasWidth * canvasHeight);

    for (let cy = 0; cy < canvasHeight; cy++) {
        for (let cx = 0; cx < canvasWidth; cx++) {
            const { dx, dy } = toDevicePixel(cx, cy, deviceWidth, deviceHeight, rotation);
            const byteIndex = (dx >> 3) + dy * rowBytes;
            const bitMask = 0x80 >> (dx & 7);
            const isBlack = (blackPlane[byteIndex] & bitMask) === 0;
            const isRed = (redPlane[byteIndex] & bitMask) === 0;
            pixels[cy * canvasWidth + cx] = isBlack ? COLOR_BLACK : isRed ? COLOR_RED : COLOR_WHITE;
        }
    }

    return pixels;
}

/** Redraw a canvas 2D context from a WHITE/BLACK/RED pixel model. */
export function renderPixelsToContext(ctx, pixels, width, height) {
    const image = ctx.createImageData(width, height);
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

/** Trigger a browser download of a packed frame as a `.eink` file. */
export function triggerFrameDownload(frame) {
    const blob = new Blob([frame], { type: "application/octet-stream" });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = "drawing" + EINK_FILE_EXTENSION;
    link.click();
    URL.revokeObjectURL(url);
}

/** Encode a byte array as base64, chunked to avoid call-stack limits on large canvases. */
function bytesToBase64(bytes) {
    let binary = "";
    const chunkSize = 8192;
    for (let i = 0; i < bytes.length; i += chunkSize) {
        binary += String.fromCharCode.apply(null, bytes.subarray(i, i + chunkSize));
    }
    return btoa(binary);
}

/** Decode a base64 string produced by `bytesToBase64()` back into bytes. */
function base64ToBytes(base64) {
    const binary = atob(base64);
    const bytes = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
    return bytes;
}

/**
 * Read and validate the autosaved canvas from `localStorage`, if any.
 *
 * @returns {?{width: number, height: number, rotation: number, pixels: Uint8Array}}
 *
 */
export function readAutosave() {
    let raw;
    try {
        raw = localStorage.getItem(AUTOSAVE_KEY);
    } catch (err) {
        return null;
    }
    if (!raw) return null;

    try {
        const saved = JSON.parse(raw);
        const savedPixels = base64ToBytes(saved.pixels);
        if (savedPixels.length !== saved.width * saved.height) return null;
        return { width: saved.width, height: saved.height, rotation: saved.rotation, pixels: savedPixels };
    } catch (err) {
        return null;
    }
}

/**
 * Save a canvas-space pixel model to `localStorage`.
 *
 * @param {number} width
 * @param {number} height
 * @param {number} rotation
 * @param {Uint8Array} pixels
 * @returns {boolean} Whether the save succeeded (false if storage is full/unavailable).
 *
 */
export function writeAutosave(width, height, rotation, pixels) {
    const payload = { width, height, rotation, pixels: bytesToBase64(pixels) };
    try {
        localStorage.setItem(AUTOSAVE_KEY, JSON.stringify(payload));
        return true;
    } catch (err) {
        return false;
    }
}
