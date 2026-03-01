#pragma once

#define LGFX_ESP32_S3_BOX_LITE
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>

#include "AppState.h"

class DisplayManager {
 public:
  bool begin(AppState& state);
  void render(const AppState& state);

 private:
  void renderDashboard(const AppState& state);
  void renderFatalScreen(const AppState& state);
  void drawField(int16_t y, const char* label, const String& value, uint16_t valueColor);
  void drawText(int16_t x, int16_t y, const String& text, uint16_t fg, uint16_t bg);
  void drawText(int16_t x, int16_t y, const char* text, uint16_t fg, uint16_t bg);

  LGFX lcd_;
  bool ready_ = false;
};
