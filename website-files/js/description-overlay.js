/**
 * description-overlay.js
 *
 * Lets the truncated summary paragraph in `.top-section` (clamped on
 * mobile, see main.css) be read in full via a "Read more" trigger that
 * opens an overlay. The overlay's text is copied from that one paragraph
 * at open time rather than duplicated in the page's HTML, so there is
 * exactly one copy of it to keep in sync.
 *
 */
(function () {
    "use strict";

    const trigger = document.getElementById("description-more-trigger");
    const overlay = document.getElementById("description-overlay");
    const overlayContent = document.getElementById("description-overlay-content");
    const closeButton = document.getElementById("description-overlay-close");
    const summary = document.querySelector(".description-summary");

    if (!trigger || !overlay || !overlayContent || !closeButton || !summary) return;

    function openOverlay() {
        overlayContent.innerHTML = "";
        const paragraph = document.createElement("p");
        paragraph.textContent = summary.textContent;
        overlayContent.appendChild(paragraph);
        overlay.hidden = false;
    }

    function closeOverlay() {
        overlay.hidden = true;
    }

    trigger.addEventListener("click", openOverlay);
    closeButton.addEventListener("click", closeOverlay);
    overlay.addEventListener("click", (event) => {
        if (event.target === overlay) closeOverlay();
    });
    document.addEventListener("keydown", (event) => {
        if (event.key === "Escape" && !overlay.hidden) closeOverlay();
    });
})();
