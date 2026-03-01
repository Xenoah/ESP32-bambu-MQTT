# CLAUDE.md

## Current Work Summary

This project was reworked to use `LovyanGFX` for the ESP32-S3-BOX-Lite display, then audited and patched for critical bugs found during a deep code review.

### What Changed (Initial LovyanGFX Rewrite)

- Removed the old `ESP32S3BoxLite` display dependency from the application code.
- Added `LovyanGFX` to `platformio.ini`.
- Rebuilt the firmware into separate responsibilities:
  - `src/AppConfig.h` — Centralized Wi-Fi, MQTT, printer, and loop timing settings.
  - `src/AppState.h` — Shared runtime state for communication and display.
  - `src/DisplayManager.h` / `src/DisplayManager.cpp` — Display initialization and all screen rendering using `LovyanGFX`.
  - `src/PrinterComm.h` / `src/PrinterComm.cpp` — Wi-Fi connection, MQTT connection, MQTT subscription, JSON parsing, and printer command publishing.
  - `src/main.cpp` — Thin coordinator. Keeps the user's required infinite loop inside `setup()`.

---

### Bug Fixes Applied (Post-Audit)

#### BUG-01 — LovyanGFX Panel Config Conflict (Critical → Fixed)

**File:** `src/DisplayManager.h`

The original code had both `#define LGFX_ESP32_S3_BOX_LITE` and `#include <LGFX_AUTODETECT.hpp>` active simultaneously. `LGFX_AUTODETECT.hpp` performs its own board detection and ignores the manual `#define`, causing a panel configuration mismatch. This was the root cause of the blank/flashing screen.

**Fix:** Removed `#define LGFX_ESP32_S3_BOX_LITE`. The autodetect path runs fully without interference.

---

#### DISP-01 — Screen Flicker from Full `fillScreen` on Every Frame (High → Fixed)

**File:** `src/DisplayManager.cpp`

Every render call erased the entire screen with `fillScreen()` then redrew fields one by one. This produced visible flicker on the LCD.

**Fix:** Added a full-screen `LGFX_Sprite` (16-bit, allocated from PSRAM). All drawing targets the sprite. When rendering is complete, `sprite_.pushSprite(0, 0)` transfers the finished frame to the LCD in one atomic DMA operation. Falls back to direct LCD drawing if sprite allocation fails.

---

#### DISP-05 — Fatal Screen Title Always Said "MQTT ERROR" (Low → Fixed)

**File:** `src/DisplayManager.cpp`

Wi-Fi failures (e.g., "WIFI TIMEOUT") were shown under the title "MQTT ERROR", which was misleading.

**Fix:** Title is now selected based on whether `errorReason` starts with "WIFI".

---

#### DESIGN-05 — `lcd_.init()` Return Value Ignored (Medium → Fixed)

**File:** `src/DisplayManager.cpp`

`ready_` was set to `true` unconditionally even if `lcd_.init()` returned `false`. Now `ready_ = lcd_.init()` and a failure is logged to Serial.

---

#### LOGIC-01 — G28 Auto-Homing on Every MQTT Connect (Critical → Fixed)

**File:** `src/PrinterComm.cpp`

`startHoming()` sent a G28 command every time MQTT connected or reconnected. If MQTT reconnected during a print, this would immediately send the printhead home and destroy the print.

**Fix:** `startHoming()` removed entirely. Connection now only sends `requestPushAll()`.

---

#### PROTO-01/02/03 — Wrong Command Name, Missing Wrapper, Missing `\n` (Critical → Fixed)

**File:** `src/PrinterComm.cpp`

- Command name was `"G_code"` — correct name is `"gcode_line"`.
- All commands were missing the required top-level wrapper object (`"print"` or `"pushing"`).
- G-code param lacked the required `\n` terminator.

**Fix:** All three issues resolved. Homing command was removed outright. `requestPushAll()` now uses the correct `{"pushing": {"command": "pushall", ...}}` structure.

---

#### PROTO-04 — `get_position` Is a Non-Existent Command (High → Fixed)

**File:** `src/PrinterComm.cpp`

`queryPosition()` sent `{"command": "get_position"}`, which is not a documented Bambu Lab MQTT command and is silently ignored by the printer.

**Fix:** Replaced with `requestPushAll()`, which sends `{"pushing": {"command": "pushall"}}` — the correct way to request a full status snapshot.

---

#### PROTO-05 — `millis()` Used as `sequence_id` (Medium → Fixed)

**File:** `src/PrinterComm.cpp` / `src/PrinterComm.h`

`millis()` wraps around after ~49 days and can produce duplicate `sequence_id` values.

**Fix:** Replaced with `uint32_t sequenceId_` counter (starts at 0, pre-incremented on each publish).

---

#### PROTO-06 — Commands Published with `retain=true` (Critical → Fixed)

**File:** `src/PrinterComm.cpp`

`mqttClient_.publish(...)` was called with the `retain` flag set to `true`. The MQTT broker (inside the printer) would store the last command and replay it to any reconnecting subscriber, potentially triggering unintended printer actions (e.g., a stale homing command being resent).

**Fix:** Changed to `retain=false`.

---

#### PROTO-07 — Wrong MQTT Username Default (High → Fixed)

**File:** `src/AppConfig.h`

`kPrinterUser` was set to the placeholder `"username"`. Bambu Lab LAN MQTT requires the username `"bblp"` (fixed value for all printers).

**Fix:** Default changed to `"bblp"` with an explanatory comment.

---

#### PROTO-09 — `homing_status` Shown as Raw Integer (Low → Fixed)

**File:** `src/PrinterComm.cpp`

The `homing_status` field from the printer is an integer (0=IDLE, 1=HOMING, 2=DONE). It was previously stored and displayed as-is.

**Fix:** Converted to readable label via `homingLabel()`.

---

#### PROTO-08 — `user_id` in Commands Used Wrong Value (Low → Fixed)

**File:** `src/PrinterComm.cpp`

Commands previously set `user_id` to the printer serial number. `user_id` is a cloud account field and is not required for LAN connections.

**Fix:** Field removed from all published commands.

---

#### DESIGN-04 — `setBufferSize()` Return Value Ignored (Medium → Fixed)

**File:** `src/PrinterComm.cpp`

If `setBufferSize(8192)` fails (out of heap), MQTT silently falls back to 256-byte messages, dropping all Bambu status payloads. Now logs a warning to Serial on failure.

Buffer size also increased from 4096 to 8192 to handle large AMS status payloads.

---

#### REL-01 / REL-03 — Intermediate `String` Allocation in MQTT Callback (High → Fixed)

**File:** `src/PrinterComm.cpp`

Every incoming MQTT message built a full `String` copy of the payload before parsing, then logged `"Message content: " + message` (creating yet another copy). Each allocation contributes to heap fragmentation during long sessions.

**Fix:**
- `deserializeJson()` now receives the raw `payload` pointer and `length` directly (no intermediate `String`).
- Serial logging no longer concatenates the message string.

---

#### LOGIC-05 / REL-06 — 5-Second Blocking Delay in MQTT Retry Loop (High → Fixed)

**File:** `src/PrinterComm.cpp`

`delay(kMqttRetryDelayMs)` (5000 ms × 5 retries = up to 25 seconds) blocked the RTOS scheduler for long periods, risking a Task Watchdog Timer reset.

**Fix:** Replaced with a loop of `delay(100)` slices that accumulate to the configured delay, allowing the RTOS to service other tasks between slices.

---

#### BUG-03 — `WiFi.macAddress()` Called Before `WiFi.mode()` (Low → Fixed)

**File:** `src/PrinterComm.cpp`

MAC address was read before the Wi-Fi stack was initialized, which can return zeros on some ESP-IDF versions.

**Fix:** `WiFi.mode(WIFI_STA)` is now called first; MAC is read immediately after.

---

### Display Behavior

- Normal screen shows: Wi-Fi status, IP, MQTT event, Bed temp, Nozzle temp, Printer Wi-Fi (dBm), Progress, Layer (current/total), Printer state, Homing status (IDLE/HOMING/DONE), Sequence ID, Printer serial, Uptime.
- Fatal screen shows: error-type title (WIFI ERROR or MQTT ERROR), reason, Wi-Fi state, IP, MQTT state.

### Failure Handling

- `ESP.restart()` is not used.
- Wi-Fi timeout → fatal screen, system halted.
- Wi-Fi disconnect after startup → fatal screen, system halted.
- MQTT max retries exceeded → fatal screen, system halted.

### Communication / Rendering Separation

- `PrinterComm` updates `AppState` only.
- `DisplayManager` renders `AppState` only.
- `main.cpp` decides when to call communication tick and when to redraw.

### Build Status

- `pio run`: success (pre-fix)
- `pio run -t upload`: success (pre-fix)
- Upload target: `COM4`
- Build status post-fix: pending verification

### Known Notes

- Uses Wi-Fi TLS with `setInsecure()` and MQTT over port `8883`.
- Printer credentials and host are placeholders until the user edits `src/AppConfig.h`.
- `kPrinterUser` is now pre-set to `"bblp"` (Bambu Lab LAN fixed value).
- `kPrinterPassword` must be set to the printer's **Access Code** (shown in the printer's LAN settings screen).
- If the screen remains blank after this fix, suspect a LovyanGFX autodetect failure — next step is a manual panel class configuration.
