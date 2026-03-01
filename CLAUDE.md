# CLAUDE.md

## Current Work Summary

This project was reworked to use `LovyanGFX` for the ESP32-S3-BOX-Lite display.

### What Changed

- Removed the old `ESP32S3BoxLite` display dependency from the application code.
- Added `LovyanGFX` to `platformio.ini`.
- Rebuilt the firmware into separate responsibilities:
  - `src/AppConfig.h`
    - Centralized Wi-Fi, MQTT, printer, and loop timing settings.
  - `src/AppState.h`
    - Shared runtime state for communication and display.
  - `src/DisplayManager.h`
  - `src/DisplayManager.cpp`
    - Display initialization and all screen rendering using `LovyanGFX`.
  - `src/PrinterComm.h`
  - `src/PrinterComm.cpp`
    - Wi-Fi connection, MQTT connection, MQTT subscription, JSON parsing, and printer command publishing.
  - `src/main.cpp`
    - Thin coordinator. Keeps the user's required infinite loop inside `setup()`.

### Display Behavior

- Normal screen shows:
  - Wi-Fi status
  - ESP32 IP address
  - MQTT/event status
  - Bed temperature
  - Nozzle temperature
  - Printer Wi-Fi signal
  - Print progress
  - Layer
  - Printer state
  - Homing status
  - Sequence ID
  - Printer serial
  - Uptime
- Fatal screen shows:
  - fixed red background
  - error reason
  - Wi-Fi state
  - IP address
  - MQTT state

### Failure Handling

- `ESP.restart()` was removed.
- Wi-Fi timeout stops the system and shows a fatal screen.
- Wi-Fi disconnect after startup stops the system and shows a fatal screen.
- MQTT retry failure stops the system and shows a fatal screen.

### Communication/Rendering Separation

- `PrinterComm` updates `AppState` only.
- `DisplayManager` renders `AppState` only.
- `main.cpp` decides when to call communication tick and when to redraw.

### Build Status

- `pio run`: success
- `pio run -t upload`: success
- Upload target: `COM4`

### Known Notes

- The project currently uses:
  - Wi-Fi TLS with `setInsecure()`
  - MQTT over port `8883`
- Printer credentials and host are placeholders until the user edits `src/AppConfig.h`.
- If the screen still flashes briefly and disappears after this rewrite, the next step should be reducing the app to a display-only diagnostic build or moving from LovyanGFX autodetect to a manual panel configuration.
