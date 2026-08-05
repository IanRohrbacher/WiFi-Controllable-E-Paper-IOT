/**
 * blocked-preview.js
 *
 * Read-only saved-canvas preview shown on `blocked.html`. Reads whatever
 * `bitmap-editor.js` last autosaved to `localStorage` (see `epaper-format.js`)
 * and renders it into a non-interactive canvas, with a button to download it
 * as a `.epaper` file. Shows an empty-state message instead if nothing has
 * been saved, or if the saved canvas no longer matches the panel's current
 * size/rotation (e.g. after a `kRotationDegrees` change).
 *
 */
import { packFrame, renderPixelsToContext, triggerFrameDownload, readAutosave } from "./epaper-format.js";

const previewCanvas = document.getElementById("blocked-canvas-preview");
if (previewCanvas) {
    const previewCtx = previewCanvas.getContext("2d");
    const emptyMessage = document.getElementById("blocked-canvas-empty");
    const downloadButton = document.getElementById("blocked-canvas-download");

    /** Canvas-space (post-rotation-swap) dimensions the panel currently expects, matching how bitmap-editor.js's resize() computes them. */
    function currentCanvasDimensions(deviceWidth, deviceHeight, rotation) {
        const swapped = rotation === 90 || rotation === 270;
        return {
            width: swapped ? deviceHeight : deviceWidth,
            height: swapped ? deviceWidth : deviceHeight,
        };
    }

    /** Show the empty-state message, hiding the preview canvas and download button. */
    function showEmpty() {
        previewCanvas.hidden = true;
        if (downloadButton) downloadButton.hidden = true;
        if (emptyMessage) emptyMessage.hidden = false;
    }

    /** Render a saved canvas into the preview and wire up its download button. */
    function showSaved(saved) {
        previewCanvas.hidden = false;
        previewCanvas.width = saved.width;
        previewCanvas.height = saved.height;
        renderPixelsToContext(previewCtx, saved.pixels, saved.width, saved.height);
        if (emptyMessage) emptyMessage.hidden = true;

        if (downloadButton) {
            downloadButton.hidden = false;
            downloadButton.addEventListener("click", () => {
                triggerFrameDownload(packFrame(saved.pixels, saved.width, saved.height, saved.rotation));
            });
        }
    }

    (async () => {
        const saved = readAutosave();
        if (!saved) {
            showEmpty();
            return;
        }

        // Default to the same compiled-in fallback bitmap-editor.js uses if this fetch fails.
        let deviceWidth = 240;
        let deviceHeight = 360;
        let rotation = 0;
        try {
            const response = await fetch("/api/display/status", { cache: "no-store" });
            const body = await response.json();
            if (body.width && body.height) {
                deviceWidth = body.width;
                deviceHeight = body.height;
                rotation = body.rotation || 0;
            }
        } catch (err) {
            // Fall back to the defaults above.
        }

        const expected = currentCanvasDimensions(deviceWidth, deviceHeight, rotation);
        if (saved.width !== expected.width || saved.height !== expected.height || saved.rotation !== rotation) {
            showEmpty();
            return;
        }

        showSaved(saved);
    })();
}
