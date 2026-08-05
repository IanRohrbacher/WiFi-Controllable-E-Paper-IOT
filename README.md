# E-Paper Badge

The badge hosts its own WiFi access point and captive portal. Connecting redirects straight to a browser-based bitmap editor where drawings are queued and flipped onto the panel in order, so the single physical display can be shared by several people, each getting a turn to showcase their drawing.

## Features

- Self-hosted WiFi access point with per-client tracking for session/timeout handling (`wifi_config`, `src/configs.h`)
- mDNS server so the badge is reachable at a friendly hostname from any browser, no IP address needed (`dns_config::kPortalHost`, `src/configs.h`)
- Captive portal that redirects supported devices straight to the editor after connecting to the access point (toggle: `web_config::kEnablePortal`, `src/configs.h`)
- Browser-based bitmap editor with adjustable brush size/opacity, four brush styles (solid, Bayer, checkerboard, striped), undo/redo, and the panel's own black/white/red palette
- Download/upload drawings as a custom file, with autosave to the browser between visits
- Per-client sessions with a lease system (up to `wifi_config::kMaxClientLeases` simultaneous clients, default 8) and a submission cooldown between sends (`wifi_config::kSubmitCooldownMs`), so the access point can't be hogged by one device
- A client can only have one frame queued at a time, and queued frames update the panel in turn on a rotating cooldown (`display_config::kDisplayCooldownMs`).
- Flash-backed (LittleFS) frame queue that survives reboots and grows with whatever flash space is free, not a fixed slot count
- Onboard IO scheduler driving any physical buttons. Current wired button allows holding through boot to wipe the flashed queue and pressing it while idle to clear the current panel/skip the current cooldown (`io_config::kClearButtonPin`, `src/io/clear_button.cpp`)
- Queued, per-module debug logging (WiFi, lease, LED, DNS, web, display, IO) flushed to Serial periodically instead of printed inline (toggle: `debug_config::kEnableVerboseLogging`, `src/logger.h`)
- Local Node.js dev server (`dev-server.js`) that mirrors the firmware's routes and mock data, for iterating on the web UI in a desktop browser with no hardware attached
- `/info` diagnostics page: connected/blocked client counts, queue depth, flash and heap usage, uptime
- Status LED patterns for setup/WiFi/DNS/e-paper failures, visible without a serial monitor
- Roughly 4-5 days of runtime per charge on the sourced 8000mAh LiPo battery

## Hardware

- [Waveshare E-Paper ESP8266 Driver Board](https://www.waveshare.com/wiki/E-Paper_ESP8266_Driver_Board) - the ESP8266 microcontroller and panel driver
- [Waveshare 3.52" e-Paper HAT (B)](https://www.waveshare.com/wiki/3.52inch_e-Paper_HAT_(B)_Manual) - the black/white/red e-paper display
- [3.7V 8000mAh LiPo battery (126090)](https://www.amazon.com/Qimoo-Battery-Rechargeable-Connector-Electronic/dp/B0CRDLSZQR) - power source, sized for roughly 4-5 days of runtime
- [USB-C 5V/2A boost converter with charging](https://www.amazon.com/DWEII-Converter-Step-Up-Charging-Protection/dp/B09YD5C9QC) - steps the LiPo's 3.7V up to the driver board's 5V input, and charges the battery over USB-C
- A momentary push button wired to `io_config::kClearButtonPin` (`D10`/GPIO1 by default, `src/configs.h`) using the ESP8266's internal pull-up - see `src/io/clear_button.cpp` for what holding vs. pressing it does
- [`badge_model.obj.zip`](badge_model.obj.zip) - OBJ + MTL model (`tinker.obj`, `obj.mtl`) for the 3D-printed badge shell, made with Tinkercad

## Project layout

- `src/` - the ESP8266 firmware: WiFi access point, captive DNS/mDNS, display driver and flash-backed frame queue, web server, status LED, IO scheduler, and the debug logging queue (`src/logger.h`)
- `lib/waveshare-epaper/` - vendored Waveshare e-paper driver for the 3.52" B panel
- `website-files/` - the web UI served from LittleFS: the bitmap editor, the blocked/session page, and the `/info` diagnostics page
- `dev-server.js` - a Node.js stand-in for the firmware's web server, for iterating on `website-files/` in a desktop browser without flashing hardware
- `platformio.ini` - PlatformIO project and board configuration

## Configuration

Everything below lives in `src/configs.h`, grouped by namespace. If you're building this yourself, this is the first file to look at before flashing.

- `wifi_config::kApSsid`/`kApPassword`/`kApChannel`/`kBroadCastAp` - the access point's identity
- `wifi_config::kMaxClientLeases` - maximum simultaneous clients (default 8)
- `wifi_config::kSessionDurationMs`/`kBlockedDurationMs` - session length and the reconnect cooldown after it ends (default 10 / 15 minutes | 0 disables either)
- `wifi_config::kSubmitCooldownMs` - minimum time between one client's frame submissions (default 1 minute)
- `dns_config::kPortalHost` - the mDNS hostname the badge is reachable at (default `portal`, i.e. `http://portal.local`)
- `display_config::kRotationDegrees` - client-side canvas rotation applied before a frame is sent (0/90/180/270)
- `display_config::kDisplayCooldownMs` - minimum time between queued frames updating the panel (default 20 minutes)
- `debug_config::kEnableVerboseLogging` and the per-module `kEnableXLogging` flags - toggle Serial debug logging overall, or one module at a time

## Requirements

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- Node.js - only needed for the optional local dev server

## Build & Flash

```bash
# Build the firmware
pio run -e waveshare_epd_esp8266

# Flash it to the board
pio run -e waveshare_epd_esp8266 --target upload

# Upload website-files/ to the board's LittleFS partition (the web UI)
pio run -e waveshare_epd_esp8266 --target uploadfs
```

## Local web preview

```bash
node dev-server.js
```

Then open `http://localhost:8080/` for the editor, or `http://localhost:8080/?blocked=1` to preview the blocked page. The dev server mirrors the firmware's routes and uses mock data, so `website-files/` can be iterated on in a normal desktop browser with no hardware attached.

## Usage

- Connect to the badge's WiFi access point (SSID/password set by `wifi_config::kApSsid`/`kApPassword`, `src/configs.h`, before flashing) and open a browser; the captive portal redirects to the editor automatically, or visit `http://portal.local` directly (hostname set by `dns_config::kPortalHost`).
- Draw with the brush tools, then hit Send to queue the drawing. Each client can submit once per cooldown (`wifi_config::kSubmitCooldownMs`) and holds one queued frame at a time. Trying to send more while you already have a frame queued offers to replace it.
- Queued frames flip onto the panel in turn on a rotating cooldown (`display_config::kDisplayCooldownMs`).
- When a session ends (`wifi_config::kSessionDurationMs`), the client is redirected to a blocked page for a cooldown period (`wifi_config::kBlockedDurationMs`) before reconnecting, with a button to download whatever they last drew. The cooldown only ticks down while disconnected from the WiFi, not while the blocked page is merely left open.
- Visit `/info` for non-sensitive diagnostics: connected clients, queue depth, flash/heap usage, and uptime.

## License

The code in this project is licensed under the MIT license.
