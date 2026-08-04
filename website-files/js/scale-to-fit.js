/**
 * scale-to-fit.js
 *
 * Uniformly shrinks `.container` (via a single CSS `transform: scale(N)`, N <=
 * 1) just enough that its natural, unclipped content height fits within
 * whatever vertical space the current viewport actually gives it, so nothing
 * gets cut off by the page's fixed-height, no-scroll layout on unusually
 * short screens. The card never grows past its designed size on large
 * screens, and since one scalar is applied to both axes, nothing (including
 * the bitmap canvas) ever gets stretched out of proportion. `transform`
 * never touches the canvas's actual pixel buffer (`canvas.width`/`height`).
 *
 * Relies on main.css giving `.container` `flex: 1 0 auto` (flex-shrink
 * disabled, no min-height:0) and no `overflow: hidden` of its own: that's
 * what makes `.container`'s own `clientHeight` reliably equal to its true
 * natural content height at all times, transform or not, and what lets
 * `body`'s own (never-transformed) `overflow: hidden` do the actual clipping
 * on the already-shrunk result instead of `.container` clipping itself in
 * its own pre-transform coordinates (which would defeat the shrink).
 *
 */
(function () {
    "use strict";

    const container = document.querySelector(".container");
    const footer = document.querySelector("footer");
    if (!container) return;

    const RECHECK_INTERVAL_MS = 500;

    /**
     * Vertical space actually available to `.container`: `body`'s fixed
     * height minus `footer`'s own height minus `.container`'s top margin.
     *
     */
    function getAvailableHeight() {
        const footerHeight = footer ? footer.getBoundingClientRect().height : 0;
        const marginTop = parseFloat(getComputedStyle(container).marginTop) || 0;
        return document.body.clientHeight - footerHeight - marginTop;
    }

    /**
     * Re-measure `.container`'s natural height against the space currently
     * available to it, and scale it down to fit if needed.
     *
     */
    function applyScale() {
        container.style.transform = "";
        container.style.marginBottom = "";

        const naturalHeight = container.clientHeight;
        const availableHeight = getAvailableHeight();
        if (naturalHeight === 0 || availableHeight <= 0) return;

        const scale = Math.min(1, availableHeight / naturalHeight);
        if (scale < 1) {
            container.style.transform = `scale(${scale})`;
            container.style.marginBottom = `-${naturalHeight * (1 - scale)}px`;
        }
    }

    window.addEventListener("resize", applyScale);
    window.addEventListener("orientationchange", applyScale);
    // Catches content-driven size changes (e.g. the bitmap canvas taking its
    // real dimensions once /api/display/status loads) without this script
    // needing to know about any page-specific element.
    setInterval(applyScale, RECHECK_INTERVAL_MS);

    applyScale();
})();
