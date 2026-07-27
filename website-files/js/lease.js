/**
 * lease.js
 *
 * Polls `/api/lease/status` every POLL_INTERVAL_MS and drives whichever
 * lease-related elements are present on the current page:
 *  - `#session-timer` (`index.html`): shows the active session countdown,
 *    and reloads the page if the session expires so the server serves
 *    `blocked.html` instead.
 *  - `#blocked-timer` (`blocked.html`): shows the reconnect-wait countdown.
 *
 * The displayed session countdown ticks down every second on the client by
 * extrapolating from the last poll. The blocked-timer is NOT extrapolated this
 * way. Per the server's design, it's frozen while the client stays connected,
 * so ticking it down client-side between polls would show a countdown that
 * doesn't match what will actually happen. It always shows exactly what the
 * server last reported.
 * 
 */
(function () {
    "use strict";

    const POLL_INTERVAL_MS = 5000; // 5 seconds
    const TICK_INTERVAL_MS = 1000; // 1 second

    /** Last status fetched from the server, and when (Date.now()) it was fetched. */
    let lastStatus = null;
    let lastFetchMs = 0;

    /**
     * @param {number} ms
     * @returns {string} "M:SS" formatted duration, floored to whole seconds.
     */
    function formatDuration(ms) {
        const totalSeconds = Math.max(0, Math.ceil(ms / 1000));
        const minutes = Math.floor(totalSeconds / 60);
        const seconds = totalSeconds % 60;
        return minutes + ":" + String(seconds).padStart(2, "0");
    }

    /** Redraw the timer element(s) from `lastStatus`, extrapolating the active countdown. */
    function render() {
        if (!lastStatus) { return; }

        const sessionTimerEl = document.getElementById("session-timer");
        const blockedTimerEl = document.getElementById("blocked-timer");

        if (lastStatus.state === "blocked") {
            if (blockedTimerEl) {
                blockedTimerEl.textContent = formatDuration(lastStatus.remainingMs);
            }
            return;
        }

        if (sessionTimerEl) {
            if (lastStatus.state === "active") {
                const elapsedSincePoll = Date.now() - lastFetchMs;
                const predictedRemaining = Math.max(0, lastStatus.remainingMs - elapsedSincePoll);
                sessionTimerEl.textContent = "Session time left: " + formatDuration(predictedRemaining);
            } else {
                sessionTimerEl.textContent = "";
            }
        }
    }

    /**
     * Fetch `/api/lease/status` and redraw. If the session-timer page
     * (`index.html`) discovers it just became blocked, reloads instead so the
     * server serves `blocked.html`.
     */
    async function pollLeaseStatus() {
        let status;
        try {
            const response = await fetch("/api/lease/status", { cache: "no-store" });
            status = await response.json();
        } catch (err) {
            return;
        }

        if (status.state === "blocked" && document.getElementById("session-timer")) {
            // Session expired while this page was open, reload and serve blocked.html instead.
            window.location.reload();
            return;
        }

        lastStatus = status;
        lastFetchMs = Date.now();
        render();
    }

    pollLeaseStatus();
    setInterval(pollLeaseStatus, POLL_INTERVAL_MS);
    setInterval(render, TICK_INTERVAL_MS);
})();
