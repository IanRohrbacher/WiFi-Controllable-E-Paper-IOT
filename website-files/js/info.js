/**
 * info.js
 *
 * Polls `/api/info/status` and fills in the basic device diagnostics shown on
 * `/info`. This page isn't linked from anywhere in the UI.
 *
 */
(function () {
    "use strict";

    const POLL_INTERVAL_MS = 5000; // 5 seconds

    /**
     * Format a millisecond duration as "XhYmZs".
     *
     * @param {number} ms
     * @returns {string}
     *
     */
    function formatUptime(ms) {
        const totalSeconds = Math.floor(ms / 1000);
        const hours = Math.floor(totalSeconds / 3600);
        const minutes = Math.floor((totalSeconds % 3600) / 60);
        const seconds = totalSeconds % 60;
        return `${hours}h ${minutes}m ${seconds}s`;
    }

    /**
     * Set an element's text content by id, if the element exists on this page.
     *
     * @param {string} id
     * @param {string} text
     *
     */
    function setText(id, text) {
        const el = document.getElementById(id);
        if (el) el.textContent = text;
    }

    async function pollInfo() {
        let status;
        try {
            const response = await fetch("/api/info/status", { cache: "no-store" });
            status = await response.json();
        } catch (err) {
            return;
        }

        setText("info-clients", `${status.clients} / ${status.maxClients}`);
        setText("info-blocked", `${status.blocked} / ${status.maxBlocked}`);
        setText("info-stale", `${status.stale} / ${status.maxStale}`);
        setText("info-frames-queued", status.framesQueued);
        setText("info-can-queue", status.canQueueNewFrame ? "Yes" : "No");
        setText("info-flash", `${status.flashUsedBytes} / ${status.flashTotalBytes} bytes`);
        setText("info-flash-chip", `${status.flashChipBytes} bytes`);
        setText("info-heap", `${status.freeHeapBytes} bytes`);
        setText("info-reset-reason", status.resetReason);
        setText("info-uptime", formatUptime(status.uptimeMs));
    }

    pollInfo();
    setInterval(pollInfo, POLL_INTERVAL_MS);
})();
