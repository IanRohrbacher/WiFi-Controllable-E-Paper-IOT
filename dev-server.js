// dev-server.js
//
// Local stand-in for the ESP8266 firmware's web server, for iterating on
// website-files/ in a normal desktop browser without flashing hardware.
// Serves the static files exactly as the device does and mocks the three
// JSON/multipart API routes bitmap.js and lease.js call.
//
// Run with:  node dev-server.js
// Then open: http://localhost:8080/            (index.html)
//            http://localhost:8080/?blocked=1  (blocked.html preview)
//
// Node built-ins only, no npm install required.

const http = require("http");
const fs = require("fs");
const path = require("path");
const { URL } = require("url");

const PORT = 8080;
const STATIC_ROOT = path.join(__dirname, "website-files");
const CONFIGS_H = path.join(__dirname, "src", "configs.h");
const EPD_HEADER = path.join(__dirname, "lib", "waveshare-epaper", "EPD_3in52b.h");

/**
 * Read a `NAME = EXPR;` constexpr's value out of a C++ source file.
 * Strips integer-literal suffixes (UL, U, L, ...) then evaluates the
 * (numeric-literal-only) expression, so `5UL * 60UL * 1000UL` works.
 * Returns fallback, with a console warning, if the name isn't found.
 */
function readConstexpr(fileText, name, fallback) {
    const match = fileText.match(new RegExp(`\\b${name}\\s*=\\s*([^;]+);`));
    if (!match) {
        console.warn(`[dev-server] could not find ${name}, using fallback ${fallback}`);
        return fallback;
    }
    const expr = match[1].replace(/(\d)(ULL|LL|UL|U|L)\b/gi, "$1");
    return Function(`"use strict"; return (${expr});`)();
}

/** Read a `#define NAME VALUE` macro's numeric value out of a C header. */
function readDefine(fileText, name, fallback) {
    const match = fileText.match(new RegExp(`#define\\s+${name}\\s+(\\d+)`));
    if (!match) {
        console.warn(`[dev-server] could not find ${name}, using fallback ${fallback}`);
        return fallback;
    }
    return Number(match[1]);
}

/**
 * Re-reads configs.h/EPD_3in52b.h fresh every call so editing
 * kRotationDegrees, panel dimensions, or the session/blocked durations takes
 * effect on the next request, without needing to restart this dev server.
 * 
 */
function readMockConfig() {
    const configsText = fs.readFileSync(CONFIGS_H, "utf8");
    const epdHeaderText = fs.readFileSync(EPD_HEADER, "utf8");
    return {
        deviceWidth: readDefine(epdHeaderText, "EPD_3IN52B_WIDTH", 240),
        deviceHeight: readDefine(epdHeaderText, "EPD_3IN52B_HEIGHT", 360),
        rotationDegrees: readConstexpr(configsText, "kRotationDegrees", 0),
        sessionDurationMs: readConstexpr(configsText, "kSessionDurationMs", 5 * 60 * 1000),
        blockedDurationMs: readConstexpr(configsText, "kBlockedDurationMs", 5 * 60 * 1000),
    };
}

// The real firmware's queue depth is gated by the amount of free flash space,
// see display.cpp's hasFreeSpaceForFrame(). mockQueueCap is an arbitrary
// stand-in just to make the "queue is full" 409 path reachable by sending a
// few frames back to back; change it at runtime via 
// 'GET /api/mock/queue-cap?value=N'. MOCK_UPDATE_DELAY_MS paces how quickly
// this mock frees up a slot.
let mockQueueCap = 5;
const MOCK_UPDATE_DELAY_MS = 3000;

const MIME_TYPES = {
    ".html": "text/html",
    ".js": "text/javascript",
    ".css": "text/css",
    ".ico": "image/x-icon",
    ".png": "image/png",
    ".webmanifest": "application/manifest+json",
};

// Live-reload: browsers open an SSE connection to LIVERELOAD_ROUTE, and any
// change under STATIC_ROOT, or to configs.h/the EPD header (see
// readMockConfig()), pushes a "reload" event to every open connection.
// Debounced since editors/OS file writes often fire several fs.watch events
// per save. Node built-ins only, same constraint as the rest of this file.
const LIVERELOAD_ROUTE = "/__livereload";
const LIVERELOAD_DEBOUNCE_MS = 100;
const liveReloadClients = new Set();
let liveReloadDebounceTimer = null;

function broadcastReload() {
    console.log(`[dev-server] change detected, reloading ${liveReloadClients.size} connected page(s)`);
    for (const res of liveReloadClients) {
        res.write("data: reload\n\n");
    }
}

function watchForReload(target) {
    fs.watch(target, { recursive: true }, () => {
        clearTimeout(liveReloadDebounceTimer);
        liveReloadDebounceTimer = setTimeout(broadcastReload, LIVERELOAD_DEBOUNCE_MS);
    });
}

watchForReload(STATIC_ROOT);
watchForReload(CONFIGS_H);
watchForReload(EPD_HEADER);

function handleLiveReload(req, res) {
    res.writeHead(200, {
        "Content-Type": "text/event-stream",
        "Cache-Control": "no-store",
        Connection: "keep-alive",
    });
    res.write("\n");
    liveReloadClients.add(res);
    req.on("close", () => liveReloadClients.delete(res));
}

/** Inject the live-reload client script just before </body> in served HTML. */
function injectLiveReload(html) {
    const script = `<script>new EventSource(${JSON.stringify(LIVERELOAD_ROUTE)}).onmessage = () => location.reload();</script>`;
    return html.includes("</body>") ? html.replace("</body>", `${script}</body>`) : html + script;
}

let sessionStartMs = Date.now();
// One entry per occupied mock queue slot: the Date.now() timestamp it will
// free up at. Length doubles as the current queued count.
let queueFreeAtTimestamps = [];

/** Milliseconds until the soonest mock queue slot frees up, floored at 0. */
function nextFreeInMs() {
    if (queueFreeAtTimestamps.length === 0) return 0;
    return Math.max(0, Math.min(...queueFreeAtTimestamps) - Date.now());
}

/** Same bitwise CRC32 (IEEE 802.3) used by bitmap.js and display.cpp. */
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

/** Pull the named multipart/form-data part's raw bytes out of a request body. */
function extractMultipartField(body, boundary, fieldName) {
    const marker = Buffer.from(`--${boundary}`);
    let start = body.indexOf(marker);
    while (start !== -1) {
        const next = body.indexOf(marker, start + marker.length);
        if (next === -1) break;

        let partStart = start + marker.length;
        if (body[partStart] === 0x0d && body[partStart + 1] === 0x0a) partStart += 2;
        let partEnd = next;
        if (body[partEnd - 2] === 0x0d && body[partEnd - 1] === 0x0a) partEnd -= 2;

        const part = body.slice(partStart, partEnd);
        const headerEnd = part.indexOf("\r\n\r\n");
        if (headerEnd !== -1) {
            const header = part.slice(0, headerEnd).toString("utf8");
            if (header.includes(`name="${fieldName}"`)) {
                return part.slice(headerEnd + 4);
            }
        }
        start = next;
    }
    return null;
}

function sendJson(res, statusCode, body) {
    const text = JSON.stringify(body);
    res.writeHead(statusCode, {
        "Content-Type": "application/json",
        "Cache-Control": "no-store",
        "Content-Length": Buffer.byteLength(text),
    });
    res.end(text);
}

function handleDisplayStatus(req, res) {
    const config = readMockConfig();
    sendJson(res, 200, { width: config.deviceWidth, height: config.deviceHeight, rotation: config.rotationDegrees });
}

/**
 * Compute the mock lease state from sessionStartMs, mirroring active then
 * blocked like the real device. Unlike the real device (which needs an
 * actual disconnect and wait), this mock auto-resets to a fresh active
 * session once the blocked duration passes, since there is no real
 * disconnect for a single reloading browser tab to wait out.
 */
function getMockLeaseState() {
    const config = readMockConfig();
    const elapsed = Date.now() - sessionStartMs;
    if (elapsed < config.sessionDurationMs) {
        return { state: "active", remainingMs: config.sessionDurationMs - elapsed };
    }
    const blockedElapsed = elapsed - config.sessionDurationMs;
    if (blockedElapsed >= config.blockedDurationMs) {
        sessionStartMs = Date.now();
        return { state: "active", remainingMs: config.sessionDurationMs };
    }
    return { state: "blocked", remainingMs: config.blockedDurationMs - blockedElapsed };
}

function handleLeaseStatus(req, res, query) {
    const forcedState = query.get("state");
    if (forcedState) {
        const remainingMs = Number(query.get("remainingMs") || 0);
        sendJson(res, 200, { state: forcedState, remainingMs });
        return;
    }

    sendJson(res, 200, getMockLeaseState());
}

/**
 * Read or change the mock queue cap at runtime. GET alone reports the current
 * value; GET with ?value=N sets it and reports the new value.
 */
function handleMockQueueCap(req, res, query) {
    const value = query.get("value");
    if (value !== null) {
        const parsed = Number(value);
        if (!Number.isInteger(parsed) || parsed < 1) {
            sendJson(res, 400, { status: "error", message: "value must be a positive integer" });
            return;
        }
        mockQueueCap = parsed;
        console.log(`[dev-server] mock queue cap set to ${mockQueueCap}`);
    }

    sendJson(res, 200, { queueCap: mockQueueCap });
}

function handleDisplayFrame(req, res) {
    const config = readMockConfig();
    const contentType = req.headers["content-type"] || "";
    const boundaryMatch = contentType.match(/boundary=(.+)$/);
    if (!boundaryMatch) {
        sendJson(res, 400, { status: "error", message: "missing multipart boundary" });
        return;
    }
    const boundary = boundaryMatch[1];

    const chunks = [];
    req.on("data", (chunk) => chunks.push(chunk));
    req.on("end", () => {
        const body = Buffer.concat(chunks);
        const frame = extractMultipartField(body, boundary, "frame");
        if (!frame || frame.length < 12) {
            sendJson(res, 400, { status: "error", message: "invalid bitmap header" });
            return;
        }

        const magic = frame.readUInt16LE(0);
        const version = frame.readUInt8(2);
        const encoding = frame.readUInt8(3);
        const width = frame.readUInt16LE(4);
        const height = frame.readUInt16LE(6);
        const headerCrc = frame.readUInt32LE(8);
        const planes = frame.slice(12);

        if (magic !== 0x4550) {
            sendJson(res, 400, { status: "error", message: "invalid bitmap header" });
            return;
        }
        if (version !== 1) {
            sendJson(res, 400, { status: "error", message: "unsupported bitmap version" });
            return;
        }
        if (encoding !== 0) {
            sendJson(res, 400, { status: "error", message: "unsupported bitmap encoding" });
            return;
        }
        if (width !== config.deviceWidth || height !== config.deviceHeight) {
            sendJson(res, 400, { status: "error", message: "frame dimensions do not match the panel" });
            return;
        }
        if (crc32(planes) !== headerCrc) {
            sendJson(res, 400, { status: "error", message: "frame checksum mismatch" });
            return;
        }

        if (queueFreeAtTimestamps.length >= mockQueueCap) {
            const retryAfterMs = nextFreeInMs();
            console.log(`[dev-server] rejected frame, mock queue is full, retry in ${retryAfterMs}ms`);
            sendJson(res, 409, {
                status: "error",
                message: "Not enough free flash space to queue a new frame, try again later.",
                retryAfterMs,
            });
            return;
        }

        const freeAt = Date.now() + MOCK_UPDATE_DELAY_MS;
        queueFreeAtTimestamps.push(freeAt);
        console.log(`[dev-server] queued frame (${queueFreeAtTimestamps.length}/${mockQueueCap} slots used)`);
        sendJson(res, 200, { status: "ok", queued: true });

        setTimeout(() => {
            const index = queueFreeAtTimestamps.indexOf(freeAt);
            if (index !== -1) queueFreeAtTimestamps.splice(index, 1);
            console.log(`[dev-server] mock panel updated (${queueFreeAtTimestamps.length}/${mockQueueCap} slots used)`);
        }, MOCK_UPDATE_DELAY_MS);
    });
}

function serveStatic(res, filePath) {
    fs.readFile(filePath, (err, data) => {
        if (err) {
            res.writeHead(404, { "Content-Type": "text/plain" });
            res.end("Not found");
            return;
        }
        const ext = path.extname(filePath);
        if (ext === ".html") {
            const html = injectLiveReload(data.toString("utf8"));
            res.writeHead(200, { "Content-Type": MIME_TYPES[ext] });
            res.end(html);
            return;
        }
        res.writeHead(200, { "Content-Type": MIME_TYPES[ext] || "application/octet-stream" });
        res.end(data);
    });
}

const server = http.createServer((req, res) => {
    const url = new URL(req.url, `http://${req.headers.host}`);

    if (url.pathname === "/api/display/status" && req.method === "GET") {
        handleDisplayStatus(req, res);
        return;
    }
    if (url.pathname === "/api/lease/status" && req.method === "GET") {
        handleLeaseStatus(req, res, url.searchParams);
        return;
    }
    if (url.pathname === "/api/display/frame" && req.method === "POST") {
        handleDisplayFrame(req, res);
        return;
    }
    if (url.pathname === "/api/mock/queue-cap" && req.method === "GET") {
        handleMockQueueCap(req, res, url.searchParams);
        return;
    }
    if (url.pathname === LIVERELOAD_ROUTE && req.method === "GET") {
        handleLiveReload(req, res);
        return;
    }

    if (url.pathname === "/") {
        const forcedBlocked = url.searchParams.get("blocked");
        const isBlocked = forcedBlocked ? true : getMockLeaseState().state === "blocked";
        const page = isBlocked ? "blocked.html" : "index.html";
        serveStatic(res, path.join(STATIC_ROOT, "html", page));
        return;
    }

    const resolved = path.normalize(path.join(STATIC_ROOT, url.pathname));
    if (!resolved.startsWith(STATIC_ROOT)) {
        res.writeHead(403, { "Content-Type": "text/plain" });
        res.end("Forbidden");
        return;
    }
    serveStatic(res, resolved);
});

server.listen(PORT, () => {
    const config = readMockConfig();
    console.log(`Dev server running at http://localhost:${PORT}/`);
    console.log(`Blocked-page preview at http://localhost:${PORT}/?blocked=1`);
    console.log(`Panel: ${config.deviceWidth}x${config.deviceHeight}, rotation ${config.rotationDegrees} (read from the repo, re-read on every request)`);
    console.log(`Session length: ${config.sessionDurationMs / 1000}s, blocked duration: ${config.blockedDurationMs / 1000}s (read from the repo, re-read on every request)`);
    console.log(`Force a lease state instantly with /api/lease/status?state=blocked&remainingMs=30000`);
    console.log(`Mock queue cap: ${mockQueueCap} (arbitrary, not read from the repo). Change it with /api/mock/queue-cap?value=N`);
    console.log(`Live-reload is on: served HTML auto-refreshes on any change under ${STATIC_ROOT}`);
});
