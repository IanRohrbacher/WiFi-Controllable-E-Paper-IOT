/**
 * bitmap-editor.js
 *
 * Interactive bitmap editor for `index.html`, targeting the Waveshare 3.52"
 * (black/white/red) e-paper panel. Renders a canvas, lets the user paint with
 * a 3-color palette and an adjustable dithered brush, then packs the result
 * (via `eink-format.js`) into the exact wire format the display module
 * expects and uploads it as multipart/form-data. The device streams and
 * compresses it straight into the frame queue as it arrives, then updates the
 * panel once it is that frame's turn.
 *
 * The same packed format can be saved to/loaded from a file, and the
 *  in-progress canvas is autosaved to `localStorage` (see `eink-format.js`) so
 * it survives a reload and is what `blocked-preview.js` shows on the blocked
 * screen.
 *
 */
import {
    COLOR_WHITE,
    COLOR_BLACK,
    PALETTE,
    EINK_FILE_EXTENSION,
    packFrame,
    parseEinkFrame,
    unpackFrameToPixels,
    renderPixelsToContext,
    triggerFrameDownload,
    readAutosave,
    writeAutosave,
} from "./eink-format.js";

/**
 * Ordered-dither lookup tables used by `ditherPasses()`, indexed by absolute
 * canvas pixel position (shifted by the current stroke's random phase, see
 * `strokePhaseX`/`strokePhaseY`). Bayer is a fine 8x8 dispersed-dot matrix for
 * a smooth grain, checkerboard is a 4x4 clustered-dot matrix at 2px-per-cell
 * resolution, and striped is 16 sequential row levels.
 *
 */
const BAYER_8X8 = [
    0, 48, 12, 60, 3, 51, 15, 63,
    32, 16, 44, 28, 35, 19, 47, 31,
    8, 56, 4, 52, 11, 59, 7, 55,
    40, 24, 36, 20, 43, 27, 39, 23,
    2, 50, 14, 62, 1, 49, 13, 61,
    34, 18, 46, 30, 33, 17, 45, 29,
    10, 58, 6, 54, 9, 57, 5, 53,
    42, 26, 38, 22, 41, 25, 37, 21,
];
const CHECKER_4X4 = [
    12, 5, 6, 13,
    4, 0, 1, 7,
    11, 3, 2, 8,
    15, 10, 9, 14,
];
const STRIPE_16 = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15];

/** How often the editor checks for unsaved changes and autosaves them. */
const AUTOSAVE_INTERVAL_MS = 5000; // 5 seconds

/** Device-native panel size and rotation; overwritten by /api/display/status. */
let deviceWidth = 240;
let deviceHeight = 360;
let rotationDegrees = 0;

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

/** Small patch showing the current color/opacity/style, independent of brush size. */
let brushPreviewCanvas = null;
let brushPreviewCtx = null;

/** Pending timer that clears the size-preview circle drawn on the main canvas. */
let brushSizePreviewHideTimer = null;

/** Current brush settings, controlled by the size/opacity/style controls. */
let brushSize = 1;
let brushOpacity = 100; // 0-100
let brushStyle = "solid"; // "solid" | "bayer" | "checkerboard" | "striped"

/** Canvas-space point the current stroke last stamped at, or null between strokes. */
let lastPaintPoint = null;

/**
 * Random offset into the dither patterns, re-rolled once per stroke (see
 * `beginStroke`). Keeps a single drag stable/flicker-free (the same pixel
 * always gets the same answer for the duration of one stroke) while letting a
 * fresh stroke over the same spot land on a different set of pattern cells.
 *
 */
let strokePhaseX = 0;
let strokePhaseY = 0;

/** True whenever `pixels` has changed since the last successful autosave. */
let dirty = false;

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

    const swapped = rotationDegrees === 90 || rotationDegrees === 270;
    canvasWidth = swapped ? deviceHeight : deviceWidth;
    canvasHeight = swapped ? deviceWidth : deviceHeight;
    pixels = new Uint8Array(canvasWidth * canvasHeight);

    canvas.width = canvasWidth;
    canvas.height = canvasHeight;
    updateBrushSizeBounds();
    render();
}

/** Redraw the full editor canvas from the pixel model. */
function render() {
    renderPixelsToContext(ctx, pixels, canvasWidth, canvasHeight);
}

/**
 * Decide whether a given absolute canvas pixel should be painted by the
 * current brush stamp, based on the selected dither style and density
 * (opacity). The pixel position is shifted by the current stroke's random
 * phase (`strokePhaseX`/`strokePhaseY`) before indexing into the pattern:
 * overlapping stamps within one stroke still re-test the same shifted
 * position instead of compounding density, but a new stroke gets a new
 * phase, so a small brush at low opacity isn't stuck forever missing every
 * pattern cell under it.
 *
 * @param {string} style "solid" | "bayer" | "checkerboard" | "striped".
 * @param {number} cx Absolute canvas x.
 * @param {number} cy Absolute canvas y.
 * @param {number} density 0..1, the brush opacity fraction.
 * @param {number} [phaseX] Defaults to the live stroke's phase; the brush
 * preview patch passes 0 instead so it renders a stable reference pattern.
 * @param {number} [phaseY] See `phaseX`.
 * @returns {boolean} Whether this pixel passes.
 *
 */
function ditherPasses(style, cx, cy, density, phaseX = strokePhaseX, phaseY = strokePhaseY) {
    if (style === "solid") return true;
    if (density <= 0) return false;
    if (density >= 1) return true;

    const px = cx + phaseX;
    const py = cy + phaseY;

    let value;
    let size;
    if (style === "bayer") {
        value = BAYER_8X8[(py & 7) * 8 + (px & 7)];
        size = 64;
    } else if (style === "checkerboard") {
        // 2-pixel-wide blocks so this reads as chunky growing squares, distinct from Bayer's fine grain.
        value = CHECKER_4X4[(Math.floor(py / 2) & 3) * 4 + (Math.floor(px / 2) & 3)];
        size = 16;
    } else if (style === "striped") {
        // Single-row levels over a 16-row repeat so this reads as growing horizontal bands.
        value = STRIPE_16[py & 15];
        size = 16;
    } else {
        return true;
    }
    return (value + 0.5) / size < density;
}

/**
 * Stamp the current brush (size/opacity/style) centered on one canvas pixel,
 * painting every pixel within its radius that also passes the dither test.
 *
 * @param {number} cx Canvas-space center x.
 * @param {number} cy Canvas-space center y.
 *
 */
function paintBrush(cx, cy) {
    const radius = brushSize / 2;
    const radiusSq = radius * radius;
    const minX = Math.max(0, Math.floor(cx - radius));
    const maxX = Math.min(canvasWidth - 1, Math.ceil(cx + radius));
    const minY = Math.max(0, Math.floor(cy - radius));
    const maxY = Math.min(canvasHeight - 1, Math.ceil(cy + radius));
    const density = brushOpacity / 100;

    for (let y = minY; y <= maxY; y++) {
        for (let x = minX; x <= maxX; x++) {
            const dx = x - cx;
            const dy = y - cy;
            if (dx * dx + dy * dy > radiusSq) continue;
            if (!ditherPasses(brushStyle, x, y, density)) continue;

            const index = y * canvasWidth + x;
            if (pixels[index] === activeColor) continue;
            pixels[index] = activeColor;
            dirty = true;
            ctx.fillStyle = PALETTE[activeColor];
            ctx.fillRect(x, y, 1, 1);
        }
    }
}

/**
 * Stamp the brush along the line from one canvas point to another, so a
 * fast stroke (where consecutive pointer events land far apart) still
 * paints a continuous line instead of isolated dots.
 *
 * @param {{x: number, y: number}} from
 * @param {{x: number, y: number}} to
 *
 */
function strokeBetween(from, to) {
    const distance = Math.hypot(to.x - from.x, to.y - from.y);
    const stepSize = Math.max(1, brushSize / 2);
    const steps = Math.max(1, Math.ceil(distance / stepSize));
    for (let i = 1; i <= steps; i++) {
        const t = i / steps;
        paintBrush(Math.round(from.x + (to.x - from.x) * t), Math.round(from.y + (to.y - from.y) * t));
    }
}

/**
 * Redraw the brush preview patch to match the current color/opacity/style,
 * so the user can see what the brush will paint before touching the canvas.
 * Always uses a fixed zero phase (rather than the live stroke phase) so the
 * patch is a stable reference pattern instead of changing on every stroke.
 *
 * @par Parameters
 * None.
 *
 */
function renderBrushPreview() {
    if (!brushPreviewCtx) return;
    const w = brushPreviewCanvas.width;
    const h = brushPreviewCanvas.height;
    const density = brushOpacity / 100;

    // White paints invisibly against a white background, so flip the
    // patch's background to black specifically when previewing white.
    const backgroundColor = activeColor === COLOR_WHITE ? COLOR_BLACK : COLOR_WHITE;

    const previewPixels = new Uint8Array(w * h);
    for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
            previewPixels[y * w + x] = ditherPasses(brushStyle, x, y, density, 0, 0) ? activeColor : backgroundColor;
        }
    }
    renderPixelsToContext(brushPreviewCtx, previewPixels, w, h);
}

/**
 * Briefly overlay a solid circle sized to the current brush at the center of
 * the editing canvas, so resizing the brush shows its actual footprint. The
 * overlay is cleared back to the real canvas content a moment after the last
 * size change.
 *
 * @par Parameters
 * None.
 *
 */
function showBrushSizePreview() {
    render();
    ctx.fillStyle = PALETTE[activeColor];
    ctx.beginPath();
    ctx.arc(canvasWidth / 2, canvasHeight / 2, brushSize / 2, 0, Math.PI * 2);
    ctx.fill();

    clearTimeout(brushSizePreviewHideTimer);
    brushSizePreviewHideTimer = setTimeout(render, 700);
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

/** Upload the current pixel model to `/api/display/frame`. The server queues it and updates the panel in its own turn. */
async function sendFrame() {
    setStatus("Sending...");
    try {
        const frame = packFrame(pixels, canvasWidth, canvasHeight, rotationDegrees);
        const formData = new FormData();
        formData.append("frame", new Blob([frame]), "frame.bin");

        const response = await fetch("/api/display/frame", {
            method: "POST",
            body: formData,
        });
        const body = await response.json().catch(() => ({}));

        if (response.ok) {
            setStatus("Your Image has been Queued.");
        } else if (response.status === 409) {
            const base = body.message || "Not enough free flash space to queue a new frame, try again later.";
            if (typeof body.retryAfterMs === "number") {
                const seconds = Math.ceil(body.retryAfterMs / 1000);
                setStatus(`${base}\nA spot should free up in about ${seconds}s.`);
            } else {
                setStatus(base);
            }
        } else {
            setStatus("Error: " + (body.message || response.statusText));
        }
    } catch (err) {
        setStatus("Error: " + err.message);
    }
    setTimeout(() => setStatus(""), 10000); // Clear the status after 10 seconds.
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
    dirty = true;
    render();
}

/** Download the current canvas as a `.eink` file. */
function downloadEinkFile() {
    triggerFrameDownload(packFrame(pixels, canvasWidth, canvasHeight, rotationDegrees));
}

/**
 * Validate and load a `.eink` file into the editor, replacing the current
 * canvas. Rejects anything that isn't a well-formed frame for the current
 * panel: only the exact bytes `packFrame()` would produce (or an equivalent)
 * are accepted.
 *
 * @param {File} file
 *
 */
async function handleEinkUpload(file) {
    let bytes;
    try {
        bytes = new Uint8Array(await file.arrayBuffer());
    } catch (err) {
        setStatus("Could not read the selected file.");
        return;
    }

    const parsed = parseEinkFrame(bytes);
    if (!parsed.ok) {
        setStatus(parsed.reason);
        return;
    }
    if (parsed.width !== deviceWidth || parsed.height !== deviceHeight) {
        setStatus(`This ${EINK_FILE_EXTENSION} file was saved for a different panel size/rotation.`);
        return;
    }

    pixels = unpackFrameToPixels(parsed.blackPlane, parsed.redPlane, deviceWidth, deviceHeight, rotationDegrees, canvasWidth, canvasHeight);
    dirty = true;
    render();
    setStatus(`Loaded ${EINK_FILE_EXTENSION} file.`);
}

/** Save the current canvas to `localStorage`, only if it has changed since the last save. */
function saveAutosave() {
    if (!dirty) return;
    if (writeAutosave(canvasWidth, canvasHeight, rotationDegrees, pixels)) dirty = false;
}

/** Restore the autosaved canvas into the editor, only if it matches the current panel size/rotation. */
function loadAutosave() {
    const saved = readAutosave();
    if (!saved || saved.width !== canvasWidth || saved.height !== canvasHeight || saved.rotation !== rotationDegrees) return;
    pixels = saved.pixels;
    render();
}

/** Keep the brush-size slider/number bounds in sync with the current canvas size. */
function updateBrushSizeBounds() {
    const maxSize = Math.max(canvasWidth, canvasHeight);
    const sizeSlider = document.getElementById("brush-size-slider");
    const sizeInput = document.getElementById("brush-size-input");
    brushSize = Math.min(brushSize, maxSize);
    if (sizeSlider) {
        sizeSlider.max = String(maxSize);
        sizeSlider.value = String(brushSize);
    }
    if (sizeInput) {
        sizeInput.max = String(maxSize);
        sizeInput.value = String(brushSize);
    }
}

/** Link a range input and a number input so either one updates the other, clamped to the range's bounds. */
function bindRangeNumberPair(rangeEl, numberEl, onChange) {
    if (!rangeEl || !numberEl) return;
    const apply = (value) => {
        const clamped = Math.min(Number(rangeEl.max), Math.max(Number(rangeEl.min), value));
        rangeEl.value = String(clamped);
        numberEl.value = String(clamped);
        onChange(clamped);
    };
    rangeEl.addEventListener("input", () => apply(Number(rangeEl.value)));
    numberEl.addEventListener("input", () => apply(Number(numberEl.value)));
}

/** Wire up palette buttons, brush controls, canvas painting, and action buttons. */
function bindControls() {
    document.querySelectorAll("[data-bitmap-color]").forEach((button) => {
        button.addEventListener("click", () => {
            activeColor = parseInt(button.getAttribute("data-bitmap-color"), 10);
            document.querySelectorAll("[data-bitmap-color]").forEach((b) => b.classList.remove("active"));
            button.classList.add("active");
            renderBrushPreview();
        });
    });

    bindRangeNumberPair(
        document.getElementById("brush-size-slider"),
        document.getElementById("brush-size-input"),
        (value) => { brushSize = value; showBrushSizePreview(); }
    );
    bindRangeNumberPair(
        document.getElementById("brush-opacity-slider"),
        document.getElementById("brush-opacity-input"),
        (value) => { brushOpacity = value; renderBrushPreview(); }
    );

    const styleSelect = document.getElementById("brush-style-select");
    if (styleSelect) {
        brushStyle = styleSelect.value;
        styleSelect.addEventListener("change", () => {
            brushStyle = styleSelect.value;
            renderBrushPreview();
        });
    }

    const clearButton = document.getElementById("bitmap-clear");
    if (clearButton) clearButton.addEventListener("click", clearCanvas);

    const sendButton = document.getElementById("bitmap-send");
    if (sendButton) sendButton.addEventListener("click", sendFrame);

    const downloadButton = document.getElementById("bitmap-download");
    if (downloadButton) downloadButton.addEventListener("click", downloadEinkFile);

    const uploadTrigger = document.getElementById("bitmap-upload-trigger");
    const uploadInput = document.getElementById("bitmap-upload-input");
    if (uploadInput) uploadInput.setAttribute("accept", EINK_FILE_EXTENSION);
    if (uploadTrigger && uploadInput) {
        uploadTrigger.addEventListener("click", () => uploadInput.click());
        uploadInput.addEventListener("change", () => {
            const file = uploadInput.files && uploadInput.files[0];
            uploadInput.value = "";
            if (file) handleEinkUpload(file);
        });
    }

    const beginStroke = (event) => {
        const point = event.touches ? event.touches[0] : event;
        const { x, y } = toCanvasCoords(point.clientX, point.clientY);
        clearTimeout(brushSizePreviewHideTimer);
        render(); // clear any lingering size-preview overlay before this stroke draws
        isPainting = true;
        lastPaintPoint = { x, y };
        strokePhaseX = Math.floor(Math.random() * 8);
        strokePhaseY = Math.floor(Math.random() * 8);
        paintBrush(x, y);
    };
    const continueStroke = (event) => {
        if (!isPainting) return;
        const point = event.touches ? event.touches[0] : event;
        const { x, y } = toCanvasCoords(point.clientX, point.clientY);
        strokeBetween(lastPaintPoint, { x, y });
        lastPaintPoint = { x, y };
    };
    const endStroke = () => {
        isPainting = false;
        lastPaintPoint = null;
    };

    canvas.addEventListener("mousedown", beginStroke);
    canvas.addEventListener("mousemove", continueStroke);
    window.addEventListener("mouseup", endStroke);

    canvas.addEventListener("touchstart", (event) => {
        beginStroke(event);
        event.preventDefault();
    });
    canvas.addEventListener("touchmove", (event) => {
        continueStroke(event);
        event.preventDefault();
    });
    canvas.addEventListener("touchend", endStroke);
}

/** Fetch the panel's real dimensions/rotation from `/api/display/status`, size the canvas to match, then restore any matching autosave. */
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
    loadAutosave();
}

canvas = document.getElementById("bitmap-canvas");
if (canvas) {
    ctx = canvas.getContext("2d");
    statusEl = document.getElementById("bitmap-status");
    brushPreviewCanvas = document.getElementById("brush-preview-canvas");
    brushPreviewCtx = brushPreviewCanvas ? brushPreviewCanvas.getContext("2d") : null;

    canvas.width = canvasWidth;
    canvas.height = canvasHeight;
    render();
    renderBrushPreview();
    bindControls();
    loadDisplayStatus();
    setInterval(saveAutosave, AUTOSAVE_INTERVAL_MS);
}
