#pragma once

#include <Arduino.h>
#include <SPI.h>

#include <cstdarg>
#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// Button identifiers
// ---------------------------------------------------------------------------

enum class ESP32S3BoxLiteButton {
  None,
  Config,
  Prev,
  Enter,
  Next,
};

// ---------------------------------------------------------------------------
// Button event (Phase 3)
// ---------------------------------------------------------------------------

enum class ButtonEvent {
  None,
  Pressed,
  LongPressed,
  Released,
};

// ---------------------------------------------------------------------------
// Beep patterns (Phase 4)
// ---------------------------------------------------------------------------

enum class BeepPattern {
  OK,
  Error,
  Double,
  Triple,
};

// ---------------------------------------------------------------------------
// Sprite (Phase 2)
// ---------------------------------------------------------------------------

class ESP32S3BoxLiteDisplay;  // forward declaration

class ESP32S3BoxLiteSprite {
 public:
  bool createSprite(int16_t w, int16_t h);
  void deleteSprite();
  void pushSprite(ESP32S3BoxLiteDisplay &disp, int16_t x, int16_t y);

  void fillScreen(uint16_t color);
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void drawPixel(int16_t x, int16_t y, uint16_t color);
  void drawText(int16_t x, int16_t y, const char *text, uint8_t scale, uint16_t fg, uint16_t bg);

  int16_t width() const { return w_; }
  int16_t height() const { return h_; }

 private:
  uint16_t *buffer_ = nullptr;
  int16_t w_ = 0;
  int16_t h_ = 0;
};

// ---------------------------------------------------------------------------
// Display (Phase 1 + Phase 7)
// ---------------------------------------------------------------------------

class ESP32S3BoxLiteDisplay {
 public:
  static constexpr uint16_t ColorBlack  = 0x0000;
  static constexpr uint16_t ColorWhite  = 0xFFFF;
  static constexpr uint16_t ColorRed    = 0xF800;
  static constexpr uint16_t ColorGreen  = 0x07E0;
  static constexpr uint16_t ColorBlue   = 0x001F;
  static constexpr uint16_t ColorCyan   = 0x07FF;
  static constexpr uint16_t ColorYellow = 0xFFE0;
  static constexpr uint16_t ColorGray   = 0x8410;
  static constexpr uint16_t ColorOrange = 0xFC00;
  static constexpr uint16_t ColorPurple = 0x780F;

  static constexpr uint16_t Width  = 320;
  static constexpr uint16_t Height = 240;

  bool begin();

  // --- Existing drawing ---
  void fillScreen(uint16_t color);
  void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
  void drawText(int16_t x, int16_t y, const char *text, uint8_t scale, uint16_t fg, uint16_t bg);
  void drawTextCentered(int16_t y, const char *text, uint8_t scale, uint16_t fg, uint16_t bg);

  // --- Phase 1 additions ---
  void setBacklight(uint8_t percent);
  void drawPixel(int16_t x, int16_t y, uint16_t color);
  void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
  void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void drawCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color);
  void fillCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color);
  void drawBitmap(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t fgColor, uint16_t bgColor);
  void drawRGBBitmap(int16_t x, int16_t y, const uint16_t *bitmap, int16_t w, int16_t h);
  void drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t percent, uint16_t fgColor, uint16_t bgColor);
  void printf(int16_t x, int16_t y, uint8_t scale, uint16_t fg, uint16_t bg, const char *fmt, ...);

  // --- Phase 7 UI helpers ---
  void showMessage(const char *text, uint16_t bgColor);
  void showError(const char *text);
  void showBootScreen(const char *appName, const char *version);
  void drawStatusBar(const char *left, const char *right, uint16_t bgColor);

  // --- Sprite support (called by ESP32S3BoxLiteSprite) ---
  void setAddressWindowPublic(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
  void sendRawBuffer(const uint8_t *buf, size_t len);

 private:
  void writeCommand(uint8_t command);
  void writeData(const uint8_t *data, size_t length);
  void writeCommandWithData(uint8_t command, const uint8_t *data, size_t length);
  void setAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
  void sendColor(uint16_t color, uint32_t count);
  void drawGlyph(int16_t x, int16_t y, char ch, uint8_t scale, uint16_t fg, uint16_t bg);

  SPIClass spi_{FSPI};
  bool initialized_ = false;
  bool backlightPwmSetup_ = false;
};

// ---------------------------------------------------------------------------
// Input (Phase 3)
// ---------------------------------------------------------------------------

class ESP32S3BoxLiteInput {
 public:
  bool begin();
  ESP32S3BoxLiteButton readRawButton();
  ESP32S3BoxLiteButton pollButtonEvent();
  int lastAdcMilliVolts() const;
  static const char *buttonName(ESP32S3BoxLiteButton button);

  // Phase 3
  ButtonEvent pollButtonEventEx();
  void setLongPressThresholdMs(uint32_t ms);
  bool isDoubleClick(ESP32S3BoxLiteButton btn);
  void setButtonCallback(void (*cb)(ESP32S3BoxLiteButton, ButtonEvent));
  ESP32S3BoxLiteButton waitForButton(uint32_t timeoutMs);

 private:
  // Basic debounce state
  uint32_t debounceStartedMs_ = 0;
  ESP32S3BoxLiteButton lastSample_ = ESP32S3BoxLiteButton::None;
  ESP32S3BoxLiteButton stableButton_ = ESP32S3BoxLiteButton::None;
  int lastAdcMilliVolts_ = 0;

  // Extended event state (Phase 3)
  uint32_t longPressThresholdMs_ = 500;
  uint32_t pressStartMs_ = 0;
  bool longPressReported_ = false;
  ESP32S3BoxLiteButton currentPressedBtn_ = ESP32S3BoxLiteButton::None;

  // Double-click state
  uint32_t lastClickMs_[5] = {0, 0, 0, 0, 0};  // indexed by button enum value

  // Callback
  void (*buttonCallback_)(ESP32S3BoxLiteButton, ButtonEvent) = nullptr;

  // Helper to convert button to index
  static int buttonIndex(ESP32S3BoxLiteButton btn);
};

// ---------------------------------------------------------------------------
// Audio (Phase 4 + Phase 5)
// ---------------------------------------------------------------------------

class ESP32S3BoxLiteAudio {
 public:
  bool begin();
  bool ready() const;
  uint32_t sampleRate() const;

  // Existing
  bool setSpeakerVolumePercent(uint8_t percent);
  bool setMicGainDb(uint8_t db);
  bool playBeep(int frequencyHz, int durationMs = 180);
  size_t readMicSamples(int16_t *buffer, size_t sampleCount);
  size_t writeSpeakerSamples(const int16_t *buffer, size_t sampleCount);
  int samplesToLevelPercent(const int16_t *buffer, size_t sampleCount) const;

  // Phase 4
  bool playWav(const uint8_t *data, size_t len);
  bool setVolumeFade(uint8_t targetPercent, uint32_t durationMs);
  void setMute(bool mute);
  void muteToggle();
  bool beepPattern(BeepPattern pattern);
  int getMicLevelPercent();
  float getMicPeakDb();
  bool setSampleRate(uint32_t hz);

  // Phase 5
  bool startMicRecord(int16_t *buffer, size_t maxSamples);
  size_t stopMicRecord();
  bool playWavFromSPIFFS(const char *path);
  bool saveMicToSPIFFS(const char *path, const int16_t *samples, size_t count);

 private:
  bool initI2cBus();
  bool initI2sBus();
  bool reinitI2sBus();
  bool writeRegister(uint8_t deviceAddress, uint8_t reg, uint8_t value);
  bool readRegister(uint8_t deviceAddress, uint8_t reg, uint8_t &value);
  bool initEs8156();
  bool startEs8156();
  bool initEs7243e();
  uint8_t micGainRegister(float db) const;

  bool initialized_ = false;
  uint8_t volumePercent_ = 65;
  uint8_t micGainDb_ = 24;
  bool muted_ = false;
  uint32_t currentSampleRate_ = 22050;

  // Recording state (Phase 5)
  int16_t *recordBuffer_ = nullptr;
  size_t recordMaxSamples_ = 0;
  size_t recordedSamples_ = 0;
  bool recording_ = false;
};

// ---------------------------------------------------------------------------
// Main class (Phase 6)
// ---------------------------------------------------------------------------

class ESP32S3BoxLite {
 public:
  bool begin(bool withDisplay = true, bool withInput = true, bool withAudio = true);
  ESP32S3BoxLiteDisplay &display();
  ESP32S3BoxLiteInput &input();
  ESP32S3BoxLiteAudio &audio();

  // Phase 6 - Power management
  void enterLightSleep(uint32_t wakeupGpioMask);
  void enterDeepSleep(uint64_t wakeupTimerUs);
  const char *getWakeupReason();

  // Phase 6 - NVS
  bool nvsSetInt(const char *key, int32_t value);
  int32_t nvsGetInt(const char *key, int32_t defaultValue);
  bool nvsSetStr(const char *key, const char *value);
  bool nvsGetStr(const char *key, char *buf, size_t bufLen);

  // Phase 6 - System info
  uint32_t getFreeHeap();
  uint32_t getCpuFreqMHz();
  const char *getResetReason();

 private:
  ESP32S3BoxLiteDisplay display_;
  ESP32S3BoxLiteInput input_;
  ESP32S3BoxLiteAudio audio_;

  bool nvsOpen(void **outHandle);
};
