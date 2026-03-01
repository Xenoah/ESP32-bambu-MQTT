#include "DisplayManager.h"

#include "AppConfig.h"

namespace {

constexpr uint16_t kColorBlack = 0x0000;
constexpr uint16_t kColorWhite = 0xFFFF;
constexpr uint16_t kColorRed = 0xD104;
constexpr uint16_t kColorBlue = 0x041F;
constexpr uint16_t kColorCyan = 0x07FF;
constexpr uint16_t kColorYellow = 0xFFE0;
constexpr uint16_t kColorOrange = 0xFD20;
constexpr uint16_t kColorGreen = 0x07E0;
constexpr uint16_t kColorGray = 0x8410;

}  // namespace

bool DisplayManager::begin(AppState& state) {
  lcd_.init();
  lcd_.setRotation(1);
  lcd_.setBrightness(255);
  ready_ = true;

  lcd_.fillScreen(kColorBlack);
  lcd_.setTextWrap(false);
  lcd_.setTextSize(2);
  drawText(16, 24, "BAMBU MQTT", kColorWhite, kColorBlack);
  lcd_.setTextSize(1);
  drawText(16, 56, "ESP32-S3-BOX-LITE", kColorCyan, kColorBlack);
  delay(800);

  state.displayDirty = true;
  return true;
}

void DisplayManager::render(const AppState& state) {
  if (!ready_) {
    return;
  }

  if (state.halted) {
    renderFatalScreen(state);
    return;
  }

  renderDashboard(state);
}

void DisplayManager::renderDashboard(const AppState& state) {
  uint16_t statusColor = kColorOrange;
  if (state.mqttStatus == "OK") {
    statusColor = kColorGreen;
  } else if (state.mqttStatus.startsWith("ERR") || state.mqttStatus == "STOP") {
    statusColor = kColorRed;
  }

  lcd_.fillScreen(kColorBlack);
  lcd_.fillRect(0, 0, lcd_.width(), 20, statusColor);
  lcd_.setTextSize(1);
  drawText(8, 6, "BAMBU MQTT", kColorBlack, statusColor);
  drawText(lcd_.width() - 72, 6, state.mqttStatus, kColorBlack, statusColor);

  drawField(28, "WIFI", state.wifiStatus, kColorWhite);
  drawField(42, "IP", state.ipAddress, kColorWhite);
  drawField(56, "MQTT", state.lastEvent, kColorWhite);

  lcd_.drawFastHLine(8, 72, lcd_.width() - 16, kColorGray);

  drawField(82, "BED", state.bedTemp, kColorYellow);
  drawField(96, "NOZZLE", state.nozzleTemp, kColorOrange);
  drawField(110, "P_WIFI", state.printerWifi, kColorWhite);
  drawField(124, "PROG", state.progress, kColorGreen);
  drawField(138, "LAYER", state.layer, kColorWhite);
  drawField(152, "STATE", state.printState, kColorWhite);
  drawField(166, "HOMING", state.homingStatus, kColorWhite);
  drawField(180, "SEQ", state.sequenceId, kColorWhite);

  lcd_.fillRect(0, 198, lcd_.width(), lcd_.height() - 198, kColorBlue);
  drawText(8, 206, "PRINTER", kColorWhite, kColorBlue);
  drawText(78, 206, AppConfig::kPrinterSerial, kColorWhite, kColorBlue);
  drawText(8, 220, "UPTIME", kColorWhite, kColorBlue);
  drawText(78, 220, String(millis() / 1000UL) + "s", kColorWhite, kColorBlue);
}

void DisplayManager::renderFatalScreen(const AppState& state) {
  lcd_.fillScreen(kColorRed);
  lcd_.setTextSize(2);
  drawText(12, 20, "MQTT ERROR", kColorWhite, kColorRed);
  lcd_.drawFastHLine(12, 52, lcd_.width() - 24, kColorWhite);

  lcd_.setTextSize(1);
  drawText(12, 68, "REASON", kColorYellow, kColorRed);
  drawText(12, 84, state.errorReason, kColorWhite, kColorRed);
  drawText(12, 112, "WIFI", kColorYellow, kColorRed);
  drawText(80, 112, state.wifiStatus, kColorWhite, kColorRed);
  drawText(12, 128, "IP", kColorYellow, kColorRed);
  drawText(80, 128, state.ipAddress, kColorWhite, kColorRed);
  drawText(12, 144, "MQTT", kColorYellow, kColorRed);
  drawText(80, 144, state.lastEvent, kColorWhite, kColorRed);
  drawText(12, 204, "FIX SETTINGS AND RESET", kColorWhite, kColorRed);
}

void DisplayManager::drawField(int16_t y, const char* label, const String& value, uint16_t valueColor) {
  drawText(8, y, label, kColorCyan, kColorBlack);
  drawText(96, y, value, valueColor, kColorBlack);
}

void DisplayManager::drawText(int16_t x, int16_t y, const String& text, uint16_t fg, uint16_t bg) {
  lcd_.setCursor(x, y);
  lcd_.setTextColor(fg, bg);
  lcd_.print(text);
}

void DisplayManager::drawText(int16_t x, int16_t y, const char* text, uint16_t fg, uint16_t bg) {
  lcd_.setCursor(x, y);
  lcd_.setTextColor(fg, bg);
  lcd_.print(text);
}
