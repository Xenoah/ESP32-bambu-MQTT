#include <Arduino.h>

#include "AppConfig.h"
#include "AppState.h"
#include "DisplayManager.h"
#include "PrinterComm.h"

namespace {

AppState appState;
DisplayManager displayManager;
PrinterComm printerComm;

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  displayManager.begin(appState);
  printerComm.begin(appState);

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
