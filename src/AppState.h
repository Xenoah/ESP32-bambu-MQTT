#pragma once

#include <Arduino.h>

struct AppState {
  String wifiStatus = "BOOT";
  String ipAddress = "--";
  String mqttStatus = "WAIT";
  String bedTemp = "--";
  String nozzleTemp = "--";
  String printerWifi = "--";
  String progress = "--";
  String layer = "--";
  String printState = "--";
  String homingStatus = "--";
  String sequenceId = "--";
  String lastEvent = "INIT";
  String errorReason = "";
  bool halted = false;
  bool displayDirty = true;
};

inline String formatTemperature(float value) {
  return String(value, 1) + " C";
}

inline String formatPercent(int value) {
  return String(value) + "%";
}
