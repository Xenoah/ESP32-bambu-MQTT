#pragma once

#include <Arduino.h>

namespace AppConfig {

constexpr char kWifiSsid[] = "ssid";
constexpr char kWifiPassword[] = "password";

constexpr char kPrinterHost[] = "printers_ip";
constexpr uint16_t kPrinterPort = 8883;
constexpr char kPrinterUser[] = "username";
constexpr char kPrinterPassword[] = "access_code";
constexpr char kPrinterSerial[] = "serialnumber";

constexpr uint32_t kWifiConnectTimeoutMs = 20000;
constexpr uint8_t kMqttMaxRetries = 5;
constexpr uint32_t kMqttRetryDelayMs = 5000;
constexpr uint32_t kActiveLoopDelayMs = 10;
constexpr uint32_t kHaltedLoopDelayMs = 250;

}  // namespace AppConfig
