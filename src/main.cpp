#include <Arduino.h>

#include "AppConfig.h"
#include "AppState.h"
#include "DisplayManager.h"
#include "PrinterComm.h"

namespace {

AppState       appState;
DisplayManager displayManager;
PrinterComm    printerComm;

// Callback installed during printerComm.begin() so that requestRender()
// can push the startup terminal log to the screen while the main loop
// has not yet started (printerComm.begin() is a blocking call).
void startupRenderFn(AppState& s) {
  displayManager.renderStartup(s);
  // displayDirty is intentionally left true; the main loop will perform
  // the first full dashboard/fatal render once startup completes.
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  displayManager.begin(appState);

  // Enable immediate screen updates for the startup terminal log.
  appState.immediateRender = startupRenderFn;
  printerComm.begin(appState);
  appState.immediateRender = nullptr;  // hand control back to the main loop

  for (;;) {
    printerComm.tick(appState);

    if (appState.displayDirty) {
      displayManager.render(appState);
      appState.displayDirty = false;
    }

    delay(appState.halted ? AppConfig::kHaltedLoopDelayMs : AppConfig::kActiveLoopDelayMs);
  }
}

void loop() {}
