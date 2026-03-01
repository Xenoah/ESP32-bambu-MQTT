#include "ESP32S3BoxLite.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdarg>

#include "driver/i2c.h"
#include "driver/i2s.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_heap_caps.h"
#include "SPIFFS.h"

namespace {

// ---------------------------------------------------------------------------
// Pin definitions
// ---------------------------------------------------------------------------

constexpr int kLcdMosiPin      = 6;
constexpr int kLcdClkPin       = 7;
constexpr int kLcdCsPin        = 5;
constexpr int kLcdDcPin        = 4;
constexpr int kLcdRstPin       = 48;
constexpr int kLcdBacklightPin = 45;

constexpr int kI2cPort    = I2C_NUM_0;
constexpr int kI2cSdaPin  = 8;
constexpr int kI2cSclPin  = 18;

constexpr int kI2sPort        = I2S_NUM_0;
constexpr int kI2sBclkPin     = 17;
constexpr int kI2sMclkPin     = 2;
constexpr int kI2sWsPin       = 47;
constexpr int kI2sDataOutPin  = 15;
constexpr int kI2sDataInPin   = 16;
constexpr int kPowerAmpPin    = 46;

constexpr int kConfigButtonPin = 0;
constexpr int kAdcButtonPin    = 1;

// ---------------------------------------------------------------------------
// Button ADC thresholds
// ---------------------------------------------------------------------------

constexpr int kButtonNextMinMv  = 720;
constexpr int kButtonNextMaxMv  = 920;
constexpr int kButtonEnterMinMv = 1880;
constexpr int kButtonEnterMaxMv = 2080;
constexpr int kButtonPrevMinMv  = 2310;
constexpr int kButtonPrevMaxMv  = 2510;
constexpr int kDebounceMs       = 25;

// ---------------------------------------------------------------------------
// Audio constants
// ---------------------------------------------------------------------------

constexpr uint32_t kDefaultSampleRate = 22050;
constexpr float    kPi = 3.14159265358979323846f;

constexpr uint8_t kEs8156Address  = 0x10;
constexpr uint8_t kEs7243eAddress = 0x20;

// ---------------------------------------------------------------------------
// LEDC backlight channel
// ---------------------------------------------------------------------------

constexpr int kBacklightLedcChannel = 0;

// ---------------------------------------------------------------------------
// NVS namespace
// ---------------------------------------------------------------------------

constexpr char kNvsNamespace[] = "esp32box";

// ---------------------------------------------------------------------------
// 5x7 pixel font (uppercase A-Z, digits, symbols, and lowercase a-z)
// ---------------------------------------------------------------------------

struct Glyph {
  char    ch;
  uint8_t rows[7];
};

constexpr Glyph kGlyphs[] = {
    // Special characters
    {' ',  {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000}},
    {'-',  {0b00000, 0b00000, 0b00000, 0b11111, 0b00000, 0b00000, 0b00000}},
    {'.',  {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00110, 0b00110}},
    {':',  {0b00000, 0b00110, 0b00110, 0b00000, 0b00110, 0b00110, 0b00000}},
    {'!',  {0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00000, 0b00100}},
    {'?',  {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b00000, 0b00100}},
    {'/',  {0b00001, 0b00010, 0b00100, 0b00100, 0b01000, 0b10000, 0b10000}},
    {'_',  {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b11111}},
    {'(',  {0b00010, 0b00100, 0b01000, 0b01000, 0b01000, 0b00100, 0b00010}},
    {')',  {0b01000, 0b00100, 0b00010, 0b00010, 0b00010, 0b00100, 0b01000}},
    {'%',  {0b11000, 0b11001, 0b00010, 0b00100, 0b01000, 0b10011, 0b00011}},
    {'+',  {0b00000, 0b00100, 0b00100, 0b11111, 0b00100, 0b00100, 0b00000}},
    {'=',  {0b00000, 0b00000, 0b11111, 0b00000, 0b11111, 0b00000, 0b00000}},
    // Digits
    {'0',  {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110}},
    {'1',  {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}},
    {'2',  {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111}},
    {'3',  {0b11110, 0b00001, 0b00001, 0b01110, 0b00001, 0b00001, 0b11110}},
    {'4',  {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010}},
    {'5',  {0b11111, 0b10000, 0b10000, 0b11110, 0b00001, 0b00001, 0b11110}},
    {'6',  {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110}},
    {'7',  {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000}},
    {'8',  {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110}},
    {'9',  {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b11100}},
    // Uppercase A-Z
    {'A',  {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}},
    {'B',  {0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110}},
    {'C',  {0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110}},
    {'D',  {0b11100, 0b10010, 0b10001, 0b10001, 0b10001, 0b10010, 0b11100}},
    {'E',  {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111}},
    {'F',  {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000}},
    {'G',  {0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01110}},
    {'H',  {0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}},
    {'I',  {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b11111}},
    {'J',  {0b11111, 0b00010, 0b00010, 0b00010, 0b00010, 0b10010, 0b01100}},
    {'K',  {0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001}},
    {'L',  {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111}},
    {'M',  {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001}},
    {'N',  {0b10001, 0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001}},
    {'O',  {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}},
    {'P',  {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000}},
    {'Q',  {0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101}},
    {'R',  {0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001}},
    {'S',  {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110}},
    {'T',  {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100}},
    {'U',  {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}},
    {'V',  {0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b01010, 0b00100}},
    {'W',  {0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b11011, 0b10001}},
    {'X',  {0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b01010, 0b10001}},
    {'Y',  {0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100}},
    {'Z',  {0b11111, 0b00010, 0b00100, 0b00100, 0b01000, 0b10000, 0b11111}},
    // Lowercase a-z
    {'a',  {0b00000, 0b00000, 0b01110, 0b00001, 0b01111, 0b10001, 0b01111}},
    {'b',  {0b10000, 0b10000, 0b10110, 0b11001, 0b10001, 0b10001, 0b11110}},
    {'c',  {0b00000, 0b00000, 0b01110, 0b10001, 0b10000, 0b10001, 0b01110}},
    {'d',  {0b00001, 0b00001, 0b01101, 0b10011, 0b10001, 0b10001, 0b01111}},
    {'e',  {0b00000, 0b00000, 0b01110, 0b10001, 0b11111, 0b10000, 0b01110}},
    {'f',  {0b00110, 0b01001, 0b01000, 0b11100, 0b01000, 0b01000, 0b01000}},
    {'g',  {0b00000, 0b01111, 0b10001, 0b10001, 0b01111, 0b00001, 0b01110}},
    {'h',  {0b10000, 0b10000, 0b10110, 0b11001, 0b10001, 0b10001, 0b10001}},
    {'i',  {0b00100, 0b00000, 0b01100, 0b00100, 0b00100, 0b00100, 0b01110}},
    {'j',  {0b00010, 0b00000, 0b00110, 0b00010, 0b00010, 0b10010, 0b01100}},
    {'k',  {0b10000, 0b10000, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010}},
    {'l',  {0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}},
    {'m',  {0b00000, 0b00000, 0b11010, 0b10101, 0b10101, 0b10001, 0b10001}},
    {'n',  {0b00000, 0b00000, 0b10110, 0b11001, 0b10001, 0b10001, 0b10001}},
    {'o',  {0b00000, 0b00000, 0b01110, 0b10001, 0b10001, 0b10001, 0b01110}},
    {'p',  {0b00000, 0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000}},
    {'q',  {0b00000, 0b01111, 0b10001, 0b10001, 0b01111, 0b00001, 0b00001}},
    {'r',  {0b00000, 0b00000, 0b10110, 0b11001, 0b10000, 0b10000, 0b10000}},
    {'s',  {0b00000, 0b00000, 0b01111, 0b10000, 0b01110, 0b00001, 0b11110}},
    {'t',  {0b01000, 0b01000, 0b11100, 0b01000, 0b01000, 0b01001, 0b00110}},
    {'u',  {0b00000, 0b00000, 0b10001, 0b10001, 0b10001, 0b10011, 0b01101}},
    {'v',  {0b00000, 0b00000, 0b10001, 0b10001, 0b01010, 0b01010, 0b00100}},
    {'w',  {0b00000, 0b00000, 0b10001, 0b10101, 0b10101, 0b10101, 0b01010}},
    {'x',  {0b00000, 0b00000, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001}},
    {'y',  {0b00000, 0b10001, 0b10001, 0b01111, 0b00001, 0b10001, 0b01110}},
    {'z',  {0b00000, 0b00000, 0b11111, 0b00010, 0b00100, 0b01000, 0b11111}},
};

const Glyph *findGlyph(char ch) {
  for (const auto &glyph : kGlyphs) {
    if (glyph.ch == ch) {
      return &glyph;
    }
  }
  return &kGlyphs[0];  // fallback to space
}

// ---------------------------------------------------------------------------
// WAV header parsing helpers (Phase 4)
// ---------------------------------------------------------------------------

struct WavHeader {
  uint32_t sampleRate;
  uint16_t numChannels;
  uint16_t bitsPerSample;
  uint32_t dataOffset;
  uint32_t dataSize;
  bool     valid;
};

WavHeader parseWavHeader(const uint8_t *data, size_t len) {
  WavHeader h{};
  if (len < 44) {
    return h;
  }
  // Check RIFF
  if (data[0] != 'R' || data[1] != 'I' || data[2] != 'F' || data[3] != 'F') {
    return h;
  }
  // Check WAVE
  if (data[8] != 'W' || data[9] != 'A' || data[10] != 'V' || data[11] != 'E') {
    return h;
  }

  // Scan for fmt  and data chunks
  size_t offset = 12;
  bool fmtFound = false;
  bool dataFound = false;

  while (offset + 8 <= len) {
    uint32_t chunkId = (uint32_t)data[offset] | ((uint32_t)data[offset + 1] << 8) |
                       ((uint32_t)data[offset + 2] << 16) | ((uint32_t)data[offset + 3] << 24);
    uint32_t chunkSize = (uint32_t)data[offset + 4] | ((uint32_t)data[offset + 5] << 8) |
                         ((uint32_t)data[offset + 6] << 16) | ((uint32_t)data[offset + 7] << 24);

    // "fmt "
    if (chunkId == 0x20746D66u) {
      if (offset + 8 + 16 > len) {
        break;
      }
      // audioFormat = data[offset+8] | data[offset+9]<<8  (should be 1 for PCM)
      h.numChannels  = (uint16_t)data[offset + 10] | ((uint16_t)data[offset + 11] << 8);
      h.sampleRate   = (uint32_t)data[offset + 12] | ((uint32_t)data[offset + 13] << 8) |
                       ((uint32_t)data[offset + 14] << 16) | ((uint32_t)data[offset + 15] << 24);
      h.bitsPerSample = (uint16_t)data[offset + 22] | ((uint16_t)data[offset + 23] << 8);
      fmtFound = true;
    }
    // "data"
    else if (chunkId == 0x61746164u) {
      h.dataOffset = static_cast<uint32_t>(offset + 8);
      h.dataSize   = chunkSize;
      dataFound = true;
    }

    offset += 8 + chunkSize;
    // Align to 2-byte boundary
    if (chunkSize & 1) {
      offset++;
    }
  }

  h.valid = fmtFound && dataFound;
  return h;
}

// ---------------------------------------------------------------------------
// WAV file write helper for SPIFFS (Phase 5)
// ---------------------------------------------------------------------------

bool writeWavFile(File &f, const int16_t *samples, size_t count, uint32_t sampleRate) {
  const uint32_t dataBytes = static_cast<uint32_t>(count) * 2U;
  const uint32_t riffSize  = 36U + dataBytes;

  // RIFF header
  const uint8_t riff[] = {'R', 'I', 'F', 'F'};
  f.write(riff, 4);
  uint8_t size4[4];
  size4[0] = riffSize & 0xFF;
  size4[1] = (riffSize >> 8) & 0xFF;
  size4[2] = (riffSize >> 16) & 0xFF;
  size4[3] = (riffSize >> 24) & 0xFF;
  f.write(size4, 4);
  const uint8_t wave[] = {'W', 'A', 'V', 'E'};
  f.write(wave, 4);

  // fmt chunk
  const uint8_t fmt[] = {'f', 'm', 't', ' '};
  f.write(fmt, 4);
  // chunk size = 16
  const uint8_t fmtSize[] = {16, 0, 0, 0};
  f.write(fmtSize, 4);
  // PCM = 1
  const uint8_t audioFmt[] = {1, 0};
  f.write(audioFmt, 2);
  // channels = 1 (mono)
  const uint8_t channels[] = {1, 0};
  f.write(channels, 2);
  // sample rate
  uint8_t sr[4];
  sr[0] = sampleRate & 0xFF;
  sr[1] = (sampleRate >> 8) & 0xFF;
  sr[2] = (sampleRate >> 16) & 0xFF;
  sr[3] = (sampleRate >> 24) & 0xFF;
  f.write(sr, 4);
  // byte rate = sampleRate * channels * bitsPerSample/8 = sampleRate * 2
  const uint32_t byteRate = sampleRate * 2U;
  uint8_t br[4];
  br[0] = byteRate & 0xFF;
  br[1] = (byteRate >> 8) & 0xFF;
  br[2] = (byteRate >> 16) & 0xFF;
  br[3] = (byteRate >> 24) & 0xFF;
  f.write(br, 4);
  // block align = 2
  const uint8_t blockAlign[] = {2, 0};
  f.write(blockAlign, 2);
  // bits per sample = 16
  const uint8_t bps[] = {16, 0};
  f.write(bps, 2);

  // data chunk
  const uint8_t dataId[] = {'d', 'a', 't', 'a'};
  f.write(dataId, 4);
  uint8_t ds[4];
  ds[0] = dataBytes & 0xFF;
  ds[1] = (dataBytes >> 8) & 0xFF;
  ds[2] = (dataBytes >> 16) & 0xFF;
  ds[3] = (dataBytes >> 24) & 0xFF;
  f.write(ds, 4);

  // PCM samples
  f.write(reinterpret_cast<const uint8_t *>(samples), dataBytes);
  return true;
}

}  // namespace

// ===========================================================================
// ESP32S3BoxLiteDisplay implementation
// ===========================================================================

bool ESP32S3BoxLiteDisplay::begin() {
  pinMode(kLcdCsPin, OUTPUT);
  pinMode(kLcdDcPin, OUTPUT);
  pinMode(kLcdRstPin, OUTPUT);
  pinMode(kLcdBacklightPin, OUTPUT);

  digitalWrite(kLcdCsPin, HIGH);
  digitalWrite(kLcdDcPin, HIGH);
  // Backlight off during init
  digitalWrite(kLcdBacklightPin, LOW);

  spi_.begin(kLcdClkPin, -1, kLcdMosiPin, kLcdCsPin);
  spi_.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));

  digitalWrite(kLcdRstPin, HIGH);
  delay(50);
  digitalWrite(kLcdRstPin, LOW);
  delay(50);
  digitalWrite(kLcdRstPin, HIGH);
  delay(150);

  writeCommand(0x01);
  delay(150);

  const uint8_t porch[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
  writeCommandWithData(0xB2, porch, sizeof(porch));

  const uint8_t gateCtrl[] = {0x35};
  writeCommandWithData(0xB7, gateCtrl, sizeof(gateCtrl));

  const uint8_t vcoms[] = {0x28};
  writeCommandWithData(0xBB, vcoms, sizeof(vcoms));

  const uint8_t lcmCtrl[] = {0x0C};
  writeCommandWithData(0xC0, lcmCtrl, sizeof(lcmCtrl));

  const uint8_t vdvVrhEn[] = {0x01, 0xFF};
  writeCommandWithData(0xC2, vdvVrhEn, sizeof(vdvVrhEn));

  const uint8_t vrhs[] = {0x10};
  writeCommandWithData(0xC3, vrhs, sizeof(vrhs));

  const uint8_t vdvs[] = {0x20};
  writeCommandWithData(0xC4, vdvs, sizeof(vdvs));

  const uint8_t frameRate[] = {0x0F};
  writeCommandWithData(0xC6, frameRate, sizeof(frameRate));

  const uint8_t powerCtrl[] = {0xA4, 0xA1};
  writeCommandWithData(0xD0, powerCtrl, sizeof(powerCtrl));

  const uint8_t positiveGamma[] = {
      0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32, 0x44,
      0x42, 0x06, 0x0E, 0x12, 0x14, 0x17,
  };
  writeCommandWithData(0xE0, positiveGamma, sizeof(positiveGamma));

  const uint8_t negativeGamma[] = {
      0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x31, 0x54,
      0x47, 0x0E, 0x1C, 0x17, 0x1B, 0x1E,
  };
  writeCommandWithData(0xE1, negativeGamma, sizeof(negativeGamma));

  const uint8_t colorMode[] = {0x55};
  writeCommandWithData(0x3A, colorMode, sizeof(colorMode));

  const uint8_t madctl[] = {0xA0};
  writeCommandWithData(0x36, madctl, sizeof(madctl));

  writeCommand(0x21);
  writeCommand(0x11);
  delay(120);
  writeCommand(0x13);
  writeCommand(0x29);
  delay(50);

  // Turn on backlight at 100%
  setBacklight(100);
  initialized_ = true;
  return true;
}

void ESP32S3BoxLiteDisplay::setBacklight(uint8_t percent) {
  if (!backlightPwmSetup_) {
    ledcSetup(kBacklightLedcChannel, 5000, 8);
    ledcAttachPin(kLcdBacklightPin, kBacklightLedcChannel);
    backlightPwmSetup_ = true;
  }
  const uint8_t duty = static_cast<uint8_t>((static_cast<uint16_t>(percent > 100 ? 100 : percent) * 255U) / 100U);
  ledcWrite(kBacklightLedcChannel, duty);
}

void ESP32S3BoxLiteDisplay::writeCommand(uint8_t command) {
  digitalWrite(kLcdDcPin, LOW);
  digitalWrite(kLcdCsPin, LOW);
  spi_.write(command);
  digitalWrite(kLcdCsPin, HIGH);
}

void ESP32S3BoxLiteDisplay::writeData(const uint8_t *data, size_t length) {
  digitalWrite(kLcdDcPin, HIGH);
  digitalWrite(kLcdCsPin, LOW);
  spi_.writeBytes(data, length);
  digitalWrite(kLcdCsPin, HIGH);
}

void ESP32S3BoxLiteDisplay::writeCommandWithData(uint8_t command, const uint8_t *data, size_t length) {
  writeCommand(command);
  writeData(data, length);
}

void ESP32S3BoxLiteDisplay::setAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  uint8_t data[4];

  writeCommand(0x2A);
  data[0] = x0 >> 8;
  data[1] = x0 & 0xFF;
  data[2] = x1 >> 8;
  data[3] = x1 & 0xFF;
  writeData(data, sizeof(data));

  writeCommand(0x2B);
  data[0] = y0 >> 8;
  data[1] = y0 & 0xFF;
  data[2] = y1 >> 8;
  data[3] = y1 & 0xFF;
  writeData(data, sizeof(data));

  writeCommand(0x2C);
}

void ESP32S3BoxLiteDisplay::setAddressWindowPublic(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  setAddressWindow(x0, y0, x1, y1);
}

void ESP32S3BoxLiteDisplay::sendRawBuffer(const uint8_t *buf, size_t len) {
  digitalWrite(kLcdDcPin, HIGH);
  digitalWrite(kLcdCsPin, LOW);
  spi_.writeBytes(buf, len);
  digitalWrite(kLcdCsPin, HIGH);
}

void ESP32S3BoxLiteDisplay::sendColor(uint16_t color, uint32_t count) {
  uint8_t buffer[128];
  for (size_t i = 0; i < sizeof(buffer); i += 2) {
    buffer[i]     = color >> 8;
    buffer[i + 1] = color & 0xFF;
  }

  digitalWrite(kLcdDcPin, HIGH);
  digitalWrite(kLcdCsPin, LOW);
  while (count > 0) {
    const uint32_t chunkPixels = std::min<uint32_t>(count, sizeof(buffer) / 2);
    spi_.writeBytes(buffer, chunkPixels * 2);
    count -= chunkPixels;
  }
  digitalWrite(kLcdCsPin, HIGH);
}

void ESP32S3BoxLiteDisplay::fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
  if (!initialized_ || x >= Width || y >= Height || w == 0 || h == 0) {
    return;
  }
  if (x + w > Width)  { w = Width  - x; }
  if (y + h > Height) { h = Height - y; }

  setAddressWindow(x, y, x + w - 1, y + h - 1);
  sendColor(color, static_cast<uint32_t>(w) * h);
}

void ESP32S3BoxLiteDisplay::fillScreen(uint16_t color) {
  fillRect(0, 0, Width, Height, color);
}

void ESP32S3BoxLiteDisplay::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (!initialized_ || x < 0 || y < 0 || x >= static_cast<int16_t>(Width) || y >= static_cast<int16_t>(Height)) {
    return;
  }
  fillRect(static_cast<uint16_t>(x), static_cast<uint16_t>(y), 1, 1, color);
}

void ESP32S3BoxLiteDisplay::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
  // Bresenham's line algorithm
  int16_t dx  =  std::abs(x1 - x0);
  int16_t dy  = -std::abs(y1 - y0);
  int16_t sx  = x0 < x1 ? 1 : -1;
  int16_t sy  = y0 < y1 ? 1 : -1;
  int16_t err = dx + dy;

  while (true) {
    drawPixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    int16_t e2 = 2 * err;
    if (e2 >= dy) {
      if (x0 == x1) { break; }
      err += dy;
      x0  += sx;
    }
    if (e2 <= dx) {
      if (y0 == y1) { break; }
      err += dx;
      y0  += sy;
    }
  }
}

void ESP32S3BoxLiteDisplay::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (w <= 0 || h <= 0) { return; }
  drawLine(x,         y,         x + w - 1, y,         color);  // top
  drawLine(x,         y + h - 1, x + w - 1, y + h - 1, color);  // bottom
  drawLine(x,         y,         x,         y + h - 1, color);  // left
  drawLine(x + w - 1, y,         x + w - 1, y + h - 1, color);  // right
}

void ESP32S3BoxLiteDisplay::drawCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
  // Bresenham's circle algorithm
  int16_t x = 0;
  int16_t y = r;
  int16_t d = 3 - 2 * r;

  auto plot = [&]() {
    drawPixel(cx + x, cy + y, color);
    drawPixel(cx - x, cy + y, color);
    drawPixel(cx + x, cy - y, color);
    drawPixel(cx - x, cy - y, color);
    drawPixel(cx + y, cy + x, color);
    drawPixel(cx - y, cy + x, color);
    drawPixel(cx + y, cy - x, color);
    drawPixel(cx - y, cy - x, color);
  };

  while (y >= x) {
    plot();
    ++x;
    if (d > 0) {
      --y;
      d += 4 * (x - y) + 10;
    } else {
      d += 4 * x + 6;
    }
  }
}

void ESP32S3BoxLiteDisplay::fillCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
  int16_t x = 0;
  int16_t y = r;
  int16_t d = 3 - 2 * r;

  auto hline = [&](int16_t ax, int16_t bx, int16_t row) {
    if (ax > bx) { int16_t t = ax; ax = bx; bx = t; }
    if (ax < 0)  { ax = 0; }
    if (bx >= static_cast<int16_t>(Width)) { bx = static_cast<int16_t>(Width) - 1; }
    if (row < 0 || row >= static_cast<int16_t>(Height)) { return; }
    fillRect(static_cast<uint16_t>(ax), static_cast<uint16_t>(row),
             static_cast<uint16_t>(bx - ax + 1), 1, color);
  };

  while (y >= x) {
    hline(cx - x, cx + x, cy + y);
    hline(cx - x, cx + x, cy - y);
    hline(cx - y, cx + y, cy + x);
    hline(cx - y, cx + y, cy - x);
    ++x;
    if (d > 0) {
      --y;
      d += 4 * (x - y) + 10;
    } else {
      d += 4 * x + 6;
    }
  }
}

void ESP32S3BoxLiteDisplay::drawBitmap(int16_t x, int16_t y, const uint8_t *bitmap,
                                        int16_t w, int16_t h, uint16_t fgColor, uint16_t bgColor) {
  if (!initialized_ || bitmap == nullptr || w <= 0 || h <= 0) {
    return;
  }
  const int16_t bytesPerRow = (w + 7) / 8;
  for (int16_t row = 0; row < h; ++row) {
    for (int16_t col = 0; col < w; ++col) {
      const int16_t byteIdx = row * bytesPerRow + col / 8;
      const uint8_t bitMask = 0x80 >> (col % 8);
      const bool    on      = bitmap[byteIdx] & bitMask;
      drawPixel(x + col, y + row, on ? fgColor : bgColor);
    }
  }
}

void ESP32S3BoxLiteDisplay::drawRGBBitmap(int16_t x, int16_t y, const uint16_t *bitmap, int16_t w, int16_t h) {
  if (!initialized_ || bitmap == nullptr || w <= 0 || h <= 0) {
    return;
  }
  // Clip
  if (x >= static_cast<int16_t>(Width) || y >= static_cast<int16_t>(Height)) {
    return;
  }
  // We send row by row to handle potential clipping
  setAddressWindow(
      static_cast<uint16_t>(std::max<int16_t>(x, 0)),
      static_cast<uint16_t>(std::max<int16_t>(y, 0)),
      static_cast<uint16_t>(std::min<int16_t>(x + w - 1, static_cast<int16_t>(Width)  - 1)),
      static_cast<uint16_t>(std::min<int16_t>(y + h - 1, static_cast<int16_t>(Height) - 1)));

  // Send pixel data (big-endian RGB565)
  constexpr size_t kBufPixels = 64;
  uint8_t buf[kBufPixels * 2];
  size_t bufIdx = 0;

  auto flush = [&]() {
    if (bufIdx > 0) {
      sendRawBuffer(buf, bufIdx);
      bufIdx = 0;
    }
  };

  digitalWrite(kLcdDcPin, HIGH);
  digitalWrite(kLcdCsPin, LOW);
  for (int16_t row = 0; row < h; ++row) {
    if (y + row < 0 || y + row >= static_cast<int16_t>(Height)) { continue; }
    for (int16_t col = 0; col < w; ++col) {
      if (x + col < 0 || x + col >= static_cast<int16_t>(Width)) { continue; }
      const uint16_t px = bitmap[row * w + col];
      buf[bufIdx++] = px >> 8;
      buf[bufIdx++] = px & 0xFF;
      if (bufIdx >= sizeof(buf)) {
        spi_.writeBytes(buf, bufIdx);
        bufIdx = 0;
      }
    }
  }
  if (bufIdx > 0) {
    spi_.writeBytes(buf, bufIdx);
  }
  digitalWrite(kLcdCsPin, HIGH);
}

void ESP32S3BoxLiteDisplay::drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h,
                                              uint8_t percent, uint16_t fgColor, uint16_t bgColor) {
  if (!initialized_ || w <= 0 || h <= 0) {
    return;
  }
  const uint8_t clampedPct = percent > 100 ? 100 : percent;
  // Background
  fillRect(static_cast<uint16_t>(x), static_cast<uint16_t>(y),
           static_cast<uint16_t>(w), static_cast<uint16_t>(h), bgColor);
  // Foreground fill
  const int16_t fillW = static_cast<int16_t>((static_cast<int32_t>(w) * clampedPct) / 100);
  if (fillW > 0) {
    fillRect(static_cast<uint16_t>(x), static_cast<uint16_t>(y),
             static_cast<uint16_t>(fillW), static_cast<uint16_t>(h), fgColor);
  }
}

void ESP32S3BoxLiteDisplay::printf(int16_t x, int16_t y, uint8_t scale, uint16_t fg, uint16_t bg, const char *fmt, ...) {
  char buf[128];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  drawText(x, y, buf, scale, fg, bg);
}

void ESP32S3BoxLiteDisplay::drawGlyph(int16_t x, int16_t y, char ch, uint8_t scale, uint16_t fg, uint16_t bg) {
  const Glyph *glyph = findGlyph(ch);
  for (uint8_t row = 0; row < 7; ++row) {
    for (uint8_t col = 0; col < 5; ++col) {
      const bool on = glyph->rows[row] & (1 << (4 - col));
      fillRect(static_cast<uint16_t>(x + col * scale), static_cast<uint16_t>(y + row * scale),
               scale, scale, on ? fg : bg);
    }
  }
}

void ESP32S3BoxLiteDisplay::drawText(int16_t x, int16_t y, const char *text, uint8_t scale, uint16_t fg, uint16_t bg) {
  if (scale == 0) { scale = 1; }
  const int16_t charWidth = 6 * scale;
  for (size_t i = 0; text[i] != '\0'; ++i) {
    drawGlyph(x, y, text[i], scale, fg, bg);
    x += charWidth;
  }
}

void ESP32S3BoxLiteDisplay::drawTextCentered(int16_t y, const char *text, uint8_t scale, uint16_t fg, uint16_t bg) {
  if (scale == 0) { scale = 1; }
  const size_t  len       = strlen(text);
  const int16_t charWidth = 6 * scale;
  const int16_t textWidth = static_cast<int16_t>(len) * charWidth - scale;
  const int16_t x         = (Width - textWidth) / 2;
  drawText(x, y, text, scale, fg, bg);
}

// Phase 7 - UI helpers

void ESP32S3BoxLiteDisplay::showMessage(const char *text, uint16_t bgColor) {
  if (!initialized_) { return; }
  fillScreen(bgColor);

  // Split text into lines at '\n' or auto-wrap at ~26 chars per line (scale=1)
  // We'll use scale=2 for readability, wrap at 13 chars (Width/12 = ~26 pixels per char)
  constexpr uint8_t kScale = 2;
  constexpr int16_t kMaxCharsPerLine = (Width - 20) / (6 * kScale);
  constexpr int16_t kLineHeight = 7 * kScale + 4;

  char lineBuf[64];
  size_t len = strlen(text);
  int16_t curY = (Height - kLineHeight * ((int16_t)((len + kMaxCharsPerLine - 1) / kMaxCharsPerLine))) / 2;
  if (curY < 4) { curY = 4; }

  size_t pos = 0;
  while (pos < len) {
    size_t end = pos + kMaxCharsPerLine;
    if (end > len) { end = len; }
    // Try to break at space
    if (end < len && text[end] != ' ') {
      size_t breakAt = end;
      while (breakAt > pos && text[breakAt] != ' ') { --breakAt; }
      if (breakAt > pos) { end = breakAt; }
    }
    const size_t lineLen = end - pos;
    memcpy(lineBuf, text + pos, lineLen);
    lineBuf[lineLen] = '\0';
    drawTextCentered(curY, lineBuf, kScale, ColorWhite, bgColor);
    curY += kLineHeight;
    pos = end;
    if (pos < len && text[pos] == ' ') { ++pos; }
  }
}

void ESP32S3BoxLiteDisplay::showError(const char *text) {
  showMessage(text, ColorRed);
}

void ESP32S3BoxLiteDisplay::showBootScreen(const char *appName, const char *version) {
  if (!initialized_) { return; }
  fillScreen(ColorBlack);

  // Title bar
  fillRect(0, 0, Width, 50, ColorBlue);
  drawTextCentered(16, appName, 2, ColorWhite, ColorBlue);

  // Version
  drawTextCentered(70, version, 1, ColorCyan, ColorBlack);

  // Divider
  fillRect(20, 90, Width - 40, 2, ColorGray);

  // Progress bar animation placeholder
  drawProgressBar(20, 110, Width - 40, 16, 100, ColorGreen, ColorGray);
  drawTextCentered(140, "Initializing...", 1, ColorWhite, ColorBlack);
}

void ESP32S3BoxLiteDisplay::drawStatusBar(const char *left, const char *right, uint16_t bgColor) {
  if (!initialized_) { return; }
  constexpr uint16_t kBarHeight = 16;
  fillRect(0, 0, Width, kBarHeight, bgColor);
  drawText(4, 4, left, 1, ColorWhite, bgColor);

  // Right-align the right string
  const size_t  rLen  = strlen(right);
  const int16_t rX    = static_cast<int16_t>(Width) - static_cast<int16_t>(rLen) * 6 - 4;
  drawText(rX, 4, right, 1, ColorWhite, bgColor);
}

// ===========================================================================
// ESP32S3BoxLiteSprite implementation
// ===========================================================================

bool ESP32S3BoxLiteSprite::createSprite(int16_t w, int16_t h) {
  deleteSprite();
  if (w <= 0 || h <= 0) { return false; }
  const size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 2U;

  // Try PSRAM first, then regular heap
  if (esp_spiram_is_initialized()) {
    buffer_ = static_cast<uint16_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (buffer_ == nullptr) {
    buffer_ = static_cast<uint16_t *>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
  }
  if (buffer_ == nullptr) { return false; }

  w_ = w;
  h_ = h;
  memset(buffer_, 0, bytes);
  return true;
}

void ESP32S3BoxLiteSprite::deleteSprite() {
  if (buffer_ != nullptr) {
    heap_caps_free(buffer_);
    buffer_ = nullptr;
  }
  w_ = 0;
  h_ = 0;
}

void ESP32S3BoxLiteSprite::fillScreen(uint16_t color) {
  if (buffer_ == nullptr) { return; }
  const int32_t total = static_cast<int32_t>(w_) * h_;
  for (int32_t i = 0; i < total; ++i) {
    buffer_[i] = color;
  }
}

void ESP32S3BoxLiteSprite::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (buffer_ == nullptr || w <= 0 || h <= 0) { return; }
  for (int16_t row = y; row < y + h; ++row) {
    if (row < 0 || row >= h_) { continue; }
    for (int16_t col = x; col < x + w; ++col) {
      if (col < 0 || col >= w_) { continue; }
      buffer_[row * w_ + col] = color;
    }
  }
}

void ESP32S3BoxLiteSprite::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (buffer_ == nullptr || x < 0 || y < 0 || x >= w_ || y >= h_) { return; }
  buffer_[y * w_ + x] = color;
}

void ESP32S3BoxLiteSprite::drawText(int16_t x, int16_t y, const char *text, uint8_t scale,
                                     uint16_t fg, uint16_t bg) {
  if (buffer_ == nullptr || text == nullptr) { return; }
  if (scale == 0) { scale = 1; }
  // Find glyphs and draw into buffer
  const int16_t charWidth = 6 * scale;
  int16_t cx = x;
  for (size_t i = 0; text[i] != '\0'; ++i) {
    const Glyph *glyph = findGlyph(text[i]);
    for (uint8_t row = 0; row < 7; ++row) {
      for (uint8_t col = 0; col < 5; ++col) {
        const bool on    = glyph->rows[row] & (1 << (4 - col));
        const uint16_t c = on ? fg : bg;
        for (uint8_t sy = 0; sy < scale; ++sy) {
          for (uint8_t sx = 0; sx < scale; ++sx) {
            drawPixel(cx + col * scale + sx, y + row * scale + sy, c);
          }
        }
      }
    }
    cx += charWidth;
  }
}

void ESP32S3BoxLiteSprite::pushSprite(ESP32S3BoxLiteDisplay &disp, int16_t x, int16_t y) {
  if (buffer_ == nullptr || w_ <= 0 || h_ <= 0) { return; }

  // Clip to display bounds
  const int16_t dispW = static_cast<int16_t>(ESP32S3BoxLiteDisplay::Width);
  const int16_t dispH = static_cast<int16_t>(ESP32S3BoxLiteDisplay::Height);

  int16_t srcX = 0, srcY = 0;
  int16_t dstX = x, dstY = y;
  int16_t copyW = w_, copyH = h_;

  if (dstX < 0) { srcX -= dstX; copyW += dstX; dstX = 0; }
  if (dstY < 0) { srcY -= dstY; copyH += dstY; dstY = 0; }
  if (dstX + copyW > dispW) { copyW = dispW - dstX; }
  if (dstY + copyH > dispH) { copyH = dispH - dstY; }

  if (copyW <= 0 || copyH <= 0) { return; }

  disp.setAddressWindowPublic(
      static_cast<uint16_t>(dstX), static_cast<uint16_t>(dstY),
      static_cast<uint16_t>(dstX + copyW - 1), static_cast<uint16_t>(dstY + copyH - 1));

  // Send row by row (convert from native uint16 to big-endian bytes)
  constexpr size_t kBufPixels = 128;
  uint8_t txBuf[kBufPixels * 2];
  size_t  bufIdx = 0;

  // We need to send via the public raw buffer method
  // Accumulate into txBuf and flush when full
  auto sendBuf = [&](size_t count) {
    disp.sendRawBuffer(txBuf, count);
    bufIdx = 0;
  };

  for (int16_t row = 0; row < copyH; ++row) {
    for (int16_t col = 0; col < copyW; ++col) {
      const uint16_t px = buffer_[(srcY + row) * w_ + (srcX + col)];
      txBuf[bufIdx++] = px >> 8;
      txBuf[bufIdx++] = px & 0xFF;
      if (bufIdx >= sizeof(txBuf)) {
        sendBuf(bufIdx);
      }
    }
  }
  if (bufIdx > 0) {
    sendBuf(bufIdx);
  }
}

// ===========================================================================
// ESP32S3BoxLiteInput implementation
// ===========================================================================

bool ESP32S3BoxLiteInput::begin() {
  pinMode(kConfigButtonPin, INPUT_PULLUP);
  analogReadResolution(12);
  analogSetPinAttenuation(kAdcButtonPin, ADC_11db);
  return true;
}

ESP32S3BoxLiteButton ESP32S3BoxLiteInput::readRawButton() {
  if (digitalRead(kConfigButtonPin) == LOW) {
    lastAdcMilliVolts_ = 0;
    return ESP32S3BoxLiteButton::Config;
  }

  lastAdcMilliVolts_ = static_cast<int>(analogReadMilliVolts(kAdcButtonPin));
  if (lastAdcMilliVolts_ >= kButtonPrevMinMv  && lastAdcMilliVolts_ <= kButtonPrevMaxMv)  { return ESP32S3BoxLiteButton::Prev;   }
  if (lastAdcMilliVolts_ >= kButtonEnterMinMv && lastAdcMilliVolts_ <= kButtonEnterMaxMv) { return ESP32S3BoxLiteButton::Enter;  }
  if (lastAdcMilliVolts_ >= kButtonNextMinMv  && lastAdcMilliVolts_ <= kButtonNextMaxMv)  { return ESP32S3BoxLiteButton::Next;   }
  return ESP32S3BoxLiteButton::None;
}

ESP32S3BoxLiteButton ESP32S3BoxLiteInput::pollButtonEvent() {
  const uint32_t now = millis();
  const ESP32S3BoxLiteButton sample = readRawButton();

  if (sample != lastSample_) {
    lastSample_          = sample;
    debounceStartedMs_   = now;
  }

  if (sample != stableButton_ && (now - debounceStartedMs_) >= kDebounceMs) {
    stableButton_ = sample;
    if (stableButton_ != ESP32S3BoxLiteButton::None) {
      return stableButton_;
    }
  }
  return ESP32S3BoxLiteButton::None;
}

int ESP32S3BoxLiteInput::lastAdcMilliVolts() const {
  return lastAdcMilliVolts_;
}

const char *ESP32S3BoxLiteInput::buttonName(ESP32S3BoxLiteButton button) {
  switch (button) {
    case ESP32S3BoxLiteButton::Config: return "CFG";
    case ESP32S3BoxLiteButton::Prev:   return "PRV";
    case ESP32S3BoxLiteButton::Enter:  return "ENT";
    case ESP32S3BoxLiteButton::Next:   return "NXT";
    case ESP32S3BoxLiteButton::None:
    default:                           return "NONE";
  }
}

// Phase 3 additions

int ESP32S3BoxLiteInput::buttonIndex(ESP32S3BoxLiteButton btn) {
  return static_cast<int>(btn);  // None=0, Config=1, Prev=2, Enter=3, Next=4
}

void ESP32S3BoxLiteInput::setLongPressThresholdMs(uint32_t ms) {
  longPressThresholdMs_ = ms;
}

ButtonEvent ESP32S3BoxLiteInput::pollButtonEventEx() {
  const uint32_t now = millis();
  const ESP32S3BoxLiteButton sample = readRawButton();

  // Debounce
  if (sample != lastSample_) {
    lastSample_        = sample;
    debounceStartedMs_ = now;
  }

  if (sample != stableButton_ && (now - debounceStartedMs_) >= kDebounceMs) {
    const ESP32S3BoxLiteButton prev = stableButton_;
    stableButton_ = sample;

    if (prev == ESP32S3BoxLiteButton::None && stableButton_ != ESP32S3BoxLiteButton::None) {
      // Press started
      pressStartMs_        = now;
      longPressReported_   = false;
      currentPressedBtn_   = stableButton_;

      // Track double-click
      const int idx = buttonIndex(currentPressedBtn_);
      if (idx >= 0 && idx < 5) {
        lastClickMs_[idx] = now;
      }

      if (buttonCallback_) {
        buttonCallback_(currentPressedBtn_, ButtonEvent::Pressed);
      }
      return ButtonEvent::Pressed;
    }

    if (prev != ESP32S3BoxLiteButton::None && stableButton_ == ESP32S3BoxLiteButton::None) {
      // Released
      const ESP32S3BoxLiteButton released = prev;
      currentPressedBtn_ = ESP32S3BoxLiteButton::None;
      if (buttonCallback_) {
        buttonCallback_(released, ButtonEvent::Released);
      }
      return ButtonEvent::Released;
    }
  }

  // Check for long press while still held
  if (stableButton_ != ESP32S3BoxLiteButton::None && !longPressReported_) {
    if ((now - pressStartMs_) >= longPressThresholdMs_) {
      longPressReported_ = true;
      if (buttonCallback_) {
        buttonCallback_(stableButton_, ButtonEvent::LongPressed);
      }
      return ButtonEvent::LongPressed;
    }
  }

  return ButtonEvent::None;
}

bool ESP32S3BoxLiteInput::isDoubleClick(ESP32S3BoxLiteButton btn) {
  const int idx = buttonIndex(btn);
  if (idx < 0 || idx >= 5) { return false; }

  const uint32_t now = millis();
  const uint32_t last = lastClickMs_[idx];
  if (last == 0) { return false; }

  const uint32_t delta = now - last;
  // Window: must be between debounce time and 400ms
  return (delta > static_cast<uint32_t>(kDebounceMs)) && (delta < 400U);
}

void ESP32S3BoxLiteInput::setButtonCallback(void (*cb)(ESP32S3BoxLiteButton, ButtonEvent)) {
  buttonCallback_ = cb;
}

ESP32S3BoxLiteButton ESP32S3BoxLiteInput::waitForButton(uint32_t timeoutMs) {
  const uint32_t startMs = millis();
  while (true) {
    const uint32_t now = millis();
    if (timeoutMs > 0 && (now - startMs) >= timeoutMs) {
      return ESP32S3BoxLiteButton::None;
    }
    const ESP32S3BoxLiteButton btn = pollButtonEvent();
    if (btn != ESP32S3BoxLiteButton::None) {
      return btn;
    }
    delay(5);
  }
}

// ===========================================================================
// ESP32S3BoxLiteAudio implementation
// ===========================================================================

bool ESP32S3BoxLiteAudio::begin() {
  if (initialized_) { return true; }

  pinMode(kPowerAmpPin, OUTPUT);
  digitalWrite(kPowerAmpPin, LOW);

  if (!initI2cBus() || !initI2sBus() || !initEs8156() || !startEs8156() || !initEs7243e()) {
    return false;
  }
  if (!setSpeakerVolumePercent(volumePercent_) || !setMicGainDb(micGainDb_)) {
    return false;
  }

  initialized_ = true;
  return true;
}

bool ESP32S3BoxLiteAudio::ready() const {
  return initialized_;
}

uint32_t ESP32S3BoxLiteAudio::sampleRate() const {
  return currentSampleRate_;
}

bool ESP32S3BoxLiteAudio::initI2cBus() {
  i2c_config_t config = {};
  config.mode             = I2C_MODE_MASTER;
  config.sda_io_num       = static_cast<gpio_num_t>(kI2cSdaPin);
  config.scl_io_num       = static_cast<gpio_num_t>(kI2cSclPin);
  config.sda_pullup_en    = GPIO_PULLUP_ENABLE;
  config.scl_pullup_en    = GPIO_PULLUP_ENABLE;
  config.master.clk_speed = 100000;

  esp_err_t err = i2c_param_config(static_cast<i2c_port_t>(kI2cPort), &config);
  if (err != ESP_OK) { return false; }
  err = i2c_driver_install(static_cast<i2c_port_t>(kI2cPort), config.mode, 0, 0, 0);
  return err == ESP_OK || err == ESP_ERR_INVALID_STATE;
}

bool ESP32S3BoxLiteAudio::initI2sBus() {
  i2s_config_t config = {};
  config.mode                = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
  config.sample_rate         = currentSampleRate_;
  config.bits_per_sample     = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format      = I2S_CHANNEL_FMT_ONLY_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags    = ESP_INTR_FLAG_LEVEL2 | ESP_INTR_FLAG_IRAM;
  config.dma_buf_count       = 3;
  config.dma_buf_len         = 1024;
  config.use_apll            = true;
  config.tx_desc_auto_clear  = true;
  config.fixed_mclk          = 0;

  esp_err_t err = i2s_driver_install(static_cast<i2s_port_t>(kI2sPort), &config, 0, nullptr);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { return false; }

  i2s_pin_config_t pins = {};
  pins.mck_io_num      = static_cast<gpio_num_t>(kI2sMclkPin);
  pins.bck_io_num      = static_cast<gpio_num_t>(kI2sBclkPin);
  pins.ws_io_num       = static_cast<gpio_num_t>(kI2sWsPin);
  pins.data_out_num    = static_cast<gpio_num_t>(kI2sDataOutPin);
  pins.data_in_num     = static_cast<gpio_num_t>(kI2sDataInPin);

  err = i2s_set_pin(static_cast<i2s_port_t>(kI2sPort), &pins);
  if (err != ESP_OK) { return false; }

  err = i2s_zero_dma_buffer(static_cast<i2s_port_t>(kI2sPort));
  return err == ESP_OK;
}

bool ESP32S3BoxLiteAudio::reinitI2sBus() {
  i2s_driver_uninstall(static_cast<i2s_port_t>(kI2sPort));
  return initI2sBus();
}

bool ESP32S3BoxLiteAudio::writeRegister(uint8_t deviceAddress, uint8_t reg, uint8_t value) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  if (cmd == nullptr) { return false; }

  esp_err_t err = ESP_OK;
  err |= i2c_master_start(cmd);
  err |= i2c_master_write_byte(cmd, deviceAddress, true);
  err |= i2c_master_write_byte(cmd, reg, true);
  err |= i2c_master_write_byte(cmd, value, true);
  err |= i2c_master_stop(cmd);
  err |= i2c_master_cmd_begin(static_cast<i2c_port_t>(kI2cPort), cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);
  return err == ESP_OK;
}

bool ESP32S3BoxLiteAudio::readRegister(uint8_t deviceAddress, uint8_t reg, uint8_t &value) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  if (cmd == nullptr) { return false; }

  esp_err_t err = ESP_OK;
  err |= i2c_master_start(cmd);
  err |= i2c_master_write_byte(cmd, deviceAddress, true);
  err |= i2c_master_write_byte(cmd, reg, true);
  err |= i2c_master_stop(cmd);
  err |= i2c_master_cmd_begin(static_cast<i2c_port_t>(kI2cPort), cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);
  if (err != ESP_OK) { return false; }

  cmd = i2c_cmd_link_create();
  if (cmd == nullptr) { return false; }

  err = ESP_OK;
  err |= i2c_master_start(cmd);
  err |= i2c_master_write_byte(cmd, deviceAddress | 0x01, true);
  err |= i2c_master_read_byte(cmd, &value, I2C_MASTER_LAST_NACK);
  err |= i2c_master_stop(cmd);
  err |= i2c_master_cmd_begin(static_cast<i2c_port_t>(kI2cPort), cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);
  return err == ESP_OK;
}

bool ESP32S3BoxLiteAudio::initEs8156() {
  const uint8_t sequence[][2] = {
      {0x02, 0x04}, {0x20, 0x2A}, {0x21, 0x3C}, {0x22, 0x00}, {0x24, 0x07}, {0x23, 0x00},
      {0x0A, 0x01}, {0x0B, 0x01}, {0x11, 0x00}, {0x14, 179},  {0x0D, 0x14}, {0x18, 0x00},
      {0x08, 0x3F}, {0x00, 0x02}, {0x00, 0x03}, {0x25, 0x20},
  };
  for (const auto &item : sequence) {
    if (!writeRegister(kEs8156Address, item[0], item[1])) { return false; }
  }
  digitalWrite(kPowerAmpPin, HIGH);
  return true;
}

bool ESP32S3BoxLiteAudio::startEs8156() {
  const uint8_t sequence[][2] = {
      {0x08, 0x3F}, {0x09, 0x00}, {0x18, 0x00}, {0x25, 0x20}, {0x22, 0x00},
      {0x21, 0x3C}, {0x19, 0x20}, {0x14, 179},
  };
  for (const auto &item : sequence) {
    if (!writeRegister(kEs8156Address, item[0], item[1])) { return false; }
  }
  digitalWrite(kPowerAmpPin, HIGH);
  return true;
}

bool ESP32S3BoxLiteAudio::initEs7243e() {
  const uint8_t sequence[][2] = {
      {0x01, 0x3A}, {0x00, 0x80}, {0xF9, 0x00}, {0x04, 0x02}, {0x04, 0x01}, {0xF9, 0x01},
      {0x00, 0x1E}, {0x01, 0x00}, {0x02, 0x00}, {0x03, 0x20}, {0x04, 0x01}, {0x0D, 0x00},
      {0x05, 0x00}, {0x06, 0x03}, {0x07, 0x00}, {0x08, 0xFF}, {0x09, 0xCA}, {0x0A, 0x85},
      {0x0B, 0x00}, {0x0E, 0xBF}, {0x0F, 0x80}, {0x14, 0x0C}, {0x15, 0x0C}, {0x17, 0x02},
      {0x18, 0x26}, {0x19, 0x77}, {0x1A, 0xF4}, {0x1B, 0x66}, {0x1C, 0x44}, {0x1E, 0x00},
      {0x1F, 0x0C}, {0x20, 0x1A}, {0x21, 0x1A}, {0x00, 0x80}, {0x01, 0x3A}, {0x16, 0x3F},
      {0x16, 0x00}, {0xF9, 0x00}, {0x04, 0x01}, {0x17, 0x01}, {0x20, 0x10}, {0x21, 0x10},
      {0x00, 0x80}, {0x01, 0x3A}, {0x16, 0x3F}, {0x16, 0x00},
  };
  for (const auto &item : sequence) {
    if (!writeRegister(kEs7243eAddress, item[0], item[1])) { return false; }
  }
  return true;
}

uint8_t ESP32S3BoxLiteAudio::micGainRegister(float db) const {
  db += 0.5f;
  if (db <= 33.0f) { return static_cast<uint8_t>(db / 3.0f); }
  if (db < 36.0f)  { return 12; }
  if (db < 37.0f)  { return 13; }
  return 14;
}

bool ESP32S3BoxLiteAudio::setSpeakerVolumePercent(uint8_t percent) {
  volumePercent_ = std::min<uint8_t>(percent, 100);
  const uint8_t regValue = static_cast<uint8_t>((static_cast<uint16_t>(volumePercent_) * 255U) / 100U);
  return writeRegister(kEs8156Address, 0x14, regValue);
}

bool ESP32S3BoxLiteAudio::setMicGainDb(uint8_t db) {
  micGainDb_ = db;
  const uint8_t reg = static_cast<uint8_t>(0x10 | micGainRegister(static_cast<float>(db)));
  return writeRegister(kEs7243eAddress, 0x20, reg) && writeRegister(kEs7243eAddress, 0x21, reg);
}

bool ESP32S3BoxLiteAudio::playBeep(int frequencyHz, int durationMs) {
  if (!initialized_ || frequencyHz <= 0 || durationMs <= 0) { return false; }

  constexpr size_t kChunkSamples = 256;
  int16_t buffer[kChunkSamples];
  const int totalSamples = static_cast<int>((sampleRate() * static_cast<uint32_t>(durationMs)) / 1000U);
  float phase = 0.0f;
  const float phaseStep = (2.0f * kPi * static_cast<float>(frequencyHz)) / static_cast<float>(sampleRate());

  int samplesLeft = totalSamples;
  while (samplesLeft > 0) {
    const int chunkSamples = std::min<int>(samplesLeft, static_cast<int>(kChunkSamples));
    for (int i = 0; i < chunkSamples; ++i) {
      const float position = static_cast<float>(totalSamples - samplesLeft + i) / static_cast<float>(totalSamples);
      float envelope = 1.0f;
      if (position < 0.08f) {
        envelope = position / 0.08f;
      } else if (position > 0.85f) {
        envelope = (1.0f - position) / 0.15f;
      }
      envelope = std::max(0.0f, envelope);
      buffer[i] = static_cast<int16_t>(std::sin(phase) * 12000.0f * envelope);
      phase += phaseStep;
      if (phase > 2.0f * kPi) { phase -= 2.0f * kPi; }
    }
    if (writeSpeakerSamples(buffer, static_cast<size_t>(chunkSamples)) != static_cast<size_t>(chunkSamples)) {
      return false;
    }
    samplesLeft -= chunkSamples;
  }

  memset(buffer, 0, sizeof(buffer));
  writeSpeakerSamples(buffer, kChunkSamples / 2);
  return true;
}

size_t ESP32S3BoxLiteAudio::readMicSamples(int16_t *buffer, size_t sampleCount) {
  if (!initialized_ || buffer == nullptr || sampleCount == 0) { return 0; }

  size_t bytesRead = 0;
  const size_t requestedBytes = sampleCount * sizeof(int16_t);
  const esp_err_t err = i2s_read(static_cast<i2s_port_t>(kI2sPort), buffer, requestedBytes, &bytesRead, portMAX_DELAY);
  if (err != ESP_OK) { return 0; }
  return bytesRead / sizeof(int16_t);
}

size_t ESP32S3BoxLiteAudio::writeSpeakerSamples(const int16_t *buffer, size_t sampleCount) {
  if (!initialized_ || buffer == nullptr || sampleCount == 0) { return 0; }

  size_t bytesWritten = 0;
  const size_t requestedBytes = sampleCount * sizeof(int16_t);
  const esp_err_t err = i2s_write(static_cast<i2s_port_t>(kI2sPort), buffer, requestedBytes, &bytesWritten, portMAX_DELAY);
  if (err != ESP_OK) { return 0; }
  return bytesWritten / sizeof(int16_t);
}

int ESP32S3BoxLiteAudio::samplesToLevelPercent(const int16_t *buffer, size_t sampleCount) const {
  if (buffer == nullptr || sampleCount == 0) { return 0; }
  int peak = 0;
  for (size_t i = 0; i < sampleCount; ++i) {
    const int value = buffer[i] < 0 ? -buffer[i] : buffer[i];
    if (value > peak) { peak = value; }
  }
  return std::min(100, (peak * 100) / 32767);
}

// Phase 4 additions

bool ESP32S3BoxLiteAudio::playWav(const uint8_t *data, size_t len) {
  if (!initialized_ || data == nullptr || len == 0) { return false; }

  const WavHeader h = parseWavHeader(data, len);
  if (!h.valid) { return false; }
  if (h.bitsPerSample != 16) { return false; }
  if (h.dataOffset + h.dataSize > len) { return false; }

  // If sample rate differs from current, update I2S
  if (h.sampleRate != currentSampleRate_) {
    currentSampleRate_ = h.sampleRate;
    i2s_set_sample_rates(static_cast<i2s_port_t>(kI2sPort), currentSampleRate_);
  }

  const int16_t *samples    = reinterpret_cast<const int16_t *>(data + h.dataOffset);
  size_t         totalSamps = h.dataSize / sizeof(int16_t);

  // For stereo, we send as-is (the I2S is mono left channel; stereo data will
  // play both L and R via interleaved samples on a mono bus - acceptable behaviour)
  constexpr size_t kChunk = 512;
  size_t offset = 0;
  while (offset < totalSamps) {
    const size_t count = std::min<size_t>(totalSamps - offset, kChunk);
    if (writeSpeakerSamples(samples + offset, count) == 0) { return false; }
    offset += count;
  }

  // Silence tail
  int16_t silence[32] = {};
  writeSpeakerSamples(silence, 32);
  return true;
}

bool ESP32S3BoxLiteAudio::setVolumeFade(uint8_t targetPercent, uint32_t durationMs) {
  if (!initialized_) { return false; }
  const uint8_t startPercent = volumePercent_;
  const uint8_t endPercent   = targetPercent > 100 ? 100 : targetPercent;

  if (durationMs == 0 || startPercent == endPercent) {
    return setSpeakerVolumePercent(endPercent);
  }

  constexpr uint32_t kSteps = 20;
  const uint32_t stepDelayMs = durationMs / kSteps;
  const int32_t  delta       = static_cast<int32_t>(endPercent) - static_cast<int32_t>(startPercent);

  for (uint32_t i = 1; i <= kSteps; ++i) {
    const uint8_t vol = static_cast<uint8_t>(startPercent + (delta * static_cast<int32_t>(i)) / static_cast<int32_t>(kSteps));
    if (!setSpeakerVolumePercent(vol)) { return false; }
    delay(stepDelayMs);
  }
  return setSpeakerVolumePercent(endPercent);
}

void ESP32S3BoxLiteAudio::setMute(bool mute) {
  muted_ = mute;
  // GPIO46 is speaker amp enable: HIGH = enabled, LOW = muted
  digitalWrite(kPowerAmpPin, mute ? LOW : HIGH);
}

void ESP32S3BoxLiteAudio::muteToggle() {
  setMute(!muted_);
}

bool ESP32S3BoxLiteAudio::beepPattern(BeepPattern pattern) {
  if (!initialized_) { return false; }
  switch (pattern) {
    case BeepPattern::OK:
      return playBeep(880, 100) && playBeep(1100, 150);
    case BeepPattern::Error:
      return playBeep(400, 200) && playBeep(300, 300);
    case BeepPattern::Double:
      return playBeep(660, 100) && playBeep(660, 100);
    case BeepPattern::Triple:
      return playBeep(660, 80) && playBeep(660, 80) && playBeep(880, 120);
    default:
      return false;
  }
}

int ESP32S3BoxLiteAudio::getMicLevelPercent() {
  if (!initialized_) { return 0; }
  constexpr size_t kSamples = 64;
  int16_t buf[kSamples];

  size_t bytesRead = 0;
  // Short timeout (non-blocking: 10ms timeout)
  const esp_err_t err = i2s_read(static_cast<i2s_port_t>(kI2sPort), buf, kSamples * sizeof(int16_t),
                                  &bytesRead, pdMS_TO_TICKS(10));
  if (err != ESP_OK || bytesRead == 0) { return 0; }
  return samplesToLevelPercent(buf, bytesRead / sizeof(int16_t));
}

float ESP32S3BoxLiteAudio::getMicPeakDb() {
  if (!initialized_) { return -120.0f; }
  constexpr size_t kSamples = 64;
  int16_t buf[kSamples];

  size_t bytesRead = 0;
  const esp_err_t err = i2s_read(static_cast<i2s_port_t>(kI2sPort), buf, kSamples * sizeof(int16_t),
                                  &bytesRead, pdMS_TO_TICKS(10));
  if (err != ESP_OK || bytesRead == 0) { return -120.0f; }

  const size_t n = bytesRead / sizeof(int16_t);
  int peak = 0;
  for (size_t i = 0; i < n; ++i) {
    const int v = buf[i] < 0 ? -buf[i] : buf[i];
    if (v > peak) { peak = v; }
  }
  if (peak == 0) { return -120.0f; }
  return 20.0f * std::log10(static_cast<float>(peak) / 32767.0f);
}

bool ESP32S3BoxLiteAudio::setSampleRate(uint32_t hz) {
  if (!initialized_ || hz == 0) { return false; }
  currentSampleRate_ = hz;
  const esp_err_t err = i2s_set_sample_rates(static_cast<i2s_port_t>(kI2sPort), hz);
  return err == ESP_OK;
}

// Phase 5 - Recording & SPIFFS

bool ESP32S3BoxLiteAudio::startMicRecord(int16_t *buffer, size_t maxSamples) {
  if (!initialized_ || buffer == nullptr || maxSamples == 0) { return false; }
  recordBuffer_     = buffer;
  recordMaxSamples_ = maxSamples;
  recordedSamples_  = 0;
  recording_        = true;
  return true;
}

size_t ESP32S3BoxLiteAudio::stopMicRecord() {
  if (!recording_) { return recordedSamples_; }

  // Read remaining samples from I2S into the buffer
  while (recording_ && recordedSamples_ < recordMaxSamples_) {
    const size_t remaining = recordMaxSamples_ - recordedSamples_;
    const size_t chunk     = std::min<size_t>(remaining, 256U);

    size_t bytesRead = 0;
    const esp_err_t err = i2s_read(static_cast<i2s_port_t>(kI2sPort),
                                    recordBuffer_ + recordedSamples_,
                                    chunk * sizeof(int16_t),
                                    &bytesRead,
                                    pdMS_TO_TICKS(50));
    if (err != ESP_OK || bytesRead == 0) { break; }
    recordedSamples_ += bytesRead / sizeof(int16_t);
  }

  recording_    = false;
  recordBuffer_ = nullptr;
  return recordedSamples_;
}

bool ESP32S3BoxLiteAudio::playWavFromSPIFFS(const char *path) {
  if (!initialized_ || path == nullptr) { return false; }

  if (!SPIFFS.begin(true)) { return false; }

  File f = SPIFFS.open(path, FILE_READ);
  if (!f) { return false; }

  const size_t fileSize = f.size();
  if (fileSize == 0) { f.close(); return false; }

  // Allocate buffer (try PSRAM first)
  uint8_t *buf = nullptr;
  if (esp_spiram_is_initialized()) {
    buf = static_cast<uint8_t *>(heap_caps_malloc(fileSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (buf == nullptr) {
    buf = static_cast<uint8_t *>(heap_caps_malloc(fileSize, MALLOC_CAP_8BIT));
  }
  if (buf == nullptr) { f.close(); return false; }

  f.read(buf, fileSize);
  f.close();

  const bool result = playWav(buf, fileSize);
  heap_caps_free(buf);
  return result;
}

bool ESP32S3BoxLiteAudio::saveMicToSPIFFS(const char *path, const int16_t *samples, size_t count) {
  if (path == nullptr || samples == nullptr || count == 0) { return false; }

  if (!SPIFFS.begin(true)) { return false; }

  File f = SPIFFS.open(path, FILE_WRITE);
  if (!f) { return false; }

  const bool ok = writeWavFile(f, samples, count, currentSampleRate_);
  f.close();
  return ok;
}

// ===========================================================================
// ESP32S3BoxLite main class
// ===========================================================================

bool ESP32S3BoxLite::begin(bool withDisplay, bool withInput, bool withAudio) {
  bool ok = true;
  if (withDisplay) { ok = display_.begin() && ok; }
  if (withInput)   { ok = input_.begin()   && ok; }
  if (withAudio)   { ok = audio_.begin()   && ok; }
  return ok;
}

ESP32S3BoxLiteDisplay &ESP32S3BoxLite::display() { return display_; }
ESP32S3BoxLiteInput   &ESP32S3BoxLite::input()   { return input_;   }
ESP32S3BoxLiteAudio   &ESP32S3BoxLite::audio()   { return audio_;   }

// Phase 6 - Power management

void ESP32S3BoxLite::enterLightSleep(uint32_t wakeupGpioMask) {
  // Configure GPIO wakeup sources
  if (wakeupGpioMask != 0) {
    for (int pin = 0; pin < 48; ++pin) {
      if (wakeupGpioMask & (1U << pin)) {
        gpio_wakeup_enable(static_cast<gpio_num_t>(pin), GPIO_INTR_LOW_LEVEL);
      }
    }
    esp_sleep_enable_gpio_wakeup();
  }
  esp_light_sleep_start();
}

void ESP32S3BoxLite::enterDeepSleep(uint64_t wakeupTimerUs) {
  if (wakeupTimerUs > 0) {
    esp_sleep_enable_timer_wakeup(wakeupTimerUs);
  }
  esp_deep_sleep_start();
}

const char *ESP32S3BoxLite::getWakeupReason() {
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER:       return "TIMER";
    case ESP_SLEEP_WAKEUP_GPIO:        return "GPIO";
    case ESP_SLEEP_WAKEUP_UART:        return "UART";
    case ESP_SLEEP_WAKEUP_EXT0:        return "EXT0";
    case ESP_SLEEP_WAKEUP_EXT1:        return "EXT1";
    case ESP_SLEEP_WAKEUP_TOUCHPAD:    return "TOUCHPAD";
    case ESP_SLEEP_WAKEUP_ULP:         return "ULP";
    case ESP_SLEEP_WAKEUP_UNDEFINED:
    default:                           return "NONE";
  }
}

// Phase 6 - NVS helpers

bool ESP32S3BoxLite::nvsOpen(void **outHandle) {
  // Initialize NVS flash if needed
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    err = nvs_flash_init();
  }
  if (err != ESP_OK) { return false; }

  nvs_handle_t handle;
  err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) { return false; }
  *outHandle = reinterpret_cast<void *>(static_cast<uintptr_t>(handle));
  return true;
}

bool ESP32S3BoxLite::nvsSetInt(const char *key, int32_t value) {
  void *h = nullptr;
  if (!nvsOpen(&h)) { return false; }
  const nvs_handle_t handle = static_cast<nvs_handle_t>(reinterpret_cast<uintptr_t>(h));
  const esp_err_t err = nvs_set_i32(handle, key, value);
  nvs_commit(handle);
  nvs_close(handle);
  return err == ESP_OK;
}

int32_t ESP32S3BoxLite::nvsGetInt(const char *key, int32_t defaultValue) {
  void *h = nullptr;
  if (!nvsOpen(&h)) { return defaultValue; }
  const nvs_handle_t handle = static_cast<nvs_handle_t>(reinterpret_cast<uintptr_t>(h));
  int32_t value = defaultValue;
  nvs_get_i32(handle, key, &value);
  nvs_close(handle);
  return value;
}

bool ESP32S3BoxLite::nvsSetStr(const char *key, const char *value) {
  void *h = nullptr;
  if (!nvsOpen(&h)) { return false; }
  const nvs_handle_t handle = static_cast<nvs_handle_t>(reinterpret_cast<uintptr_t>(h));
  const esp_err_t err = nvs_set_str(handle, key, value);
  nvs_commit(handle);
  nvs_close(handle);
  return err == ESP_OK;
}

bool ESP32S3BoxLite::nvsGetStr(const char *key, char *buf, size_t bufLen) {
  void *h = nullptr;
  if (!nvsOpen(&h)) { return false; }
  const nvs_handle_t handle = static_cast<nvs_handle_t>(reinterpret_cast<uintptr_t>(h));
  size_t requiredLen = bufLen;
  const esp_err_t err = nvs_get_str(handle, key, buf, &requiredLen);
  nvs_close(handle);
  return err == ESP_OK;
}

// Phase 6 - System info

uint32_t ESP32S3BoxLite::getFreeHeap() {
  return static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
}

uint32_t ESP32S3BoxLite::getCpuFreqMHz() {
  return static_cast<uint32_t>(getCpuFrequencyMhz());
}

const char *ESP32S3BoxLite::getResetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "POWER_ON";
    case ESP_RST_EXT:       return "EXT_RESET";
    case ESP_RST_SW:        return "SOFTWARE";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    case ESP_RST_UNKNOWN:
    default:                return "UNKNOWN";
  }
}
