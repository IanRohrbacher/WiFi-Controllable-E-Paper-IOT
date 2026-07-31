/**
 * blocked-preview.js
 *
 * Read-only saved-canvas preview shown on `blocked.html`. Reads whatever
 * `bitmap-editor.js` last autosaved to `localStorage` (see `eink-format.js`)
 * and renders it into a non-interactive canvas, with a button to download it
 * as a `.eink` file. Shows an empty-state message instead if nothing has been
 * saved yet.
 *
 */
import { packFrame, renderPixelsToContext, triggerFrameDownload, readAutosave } from "./eink-format.js";

const previewCanvas = document.getElementById("blocked-canvas-preview");
if (previewCanvas) {
    const previewCtx = previewCanvas.getContext("2d");
    const emptyMessage = document.getElementById("blocked-canvas-empty");
    const downloadButton = document.getElementById("blocked-canvas-download");
    const saved = readAutosave();

    if (!saved) {
        previewCanvas.hidden = true;
        if (downloadButton) downloadButton.hidden = true;
        if (emptyMessage) emptyMessage.hidden = false;
    } else {
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
}
