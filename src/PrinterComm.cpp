#include "PrinterComm.h"

#include "AppConfig.h"

namespace {

String mqttErrorLabel(int errorCode) {
  return "ERR " + String(errorCode);
}

}  // namespace

PrinterComm* PrinterComm::instance_ = nullptr;

PrinterComm::PrinterComm() : mqttClient_(tlsClient_) {}

void PrinterComm::begin(AppState& state) {
  state_ = &state;
  instance_ = this;

  snprintf(topicSubscribe_, sizeof(topicSubscribe_), "device/%s/report", AppConfig::kPrinterSerial);
  snprintf(topicPublish_, sizeof(topicPublish_), "device/%s/request", AppConfig::kPrinterSerial);

  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(mqttClientId_, sizeof(mqttClientId_), "ESP32Client-%02X%02X%02X", mac[3], mac[4], mac[5]);

  tlsClient_.setInsecure();
  mqttClient_.setServer(AppConfig::kPrinterHost, AppConfig::kPrinterPort);
  mqttClient_.setCallback(mqttCallback);
  mqttClient_.setBufferSize(4096);
  mqttClient_.setKeepAlive(60);

  setupWiFi(state);
}

void PrinterComm::tick(AppState& state) {
  if (state.halted) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    setFatalState(state, "WIFI LOST");
    return;
  }

  ensureMqtt(state);
  if (!state.halted && mqttClient_.connected()) {
    mqttClient_.loop();
  }
}

void PrinterComm::mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (instance_ != nullptr) {
    instance_->onMessage(topic, payload, length);
  }
}

void PrinterComm::setupWiFi(AppState& state) {
  state.wifiStatus = "CONNECT";
  state.lastEvent = "JOIN WIFI";
  requestRender(state);

  WiFi.mode(WIFI_STA);
  WiFi.begin(AppConfig::kWifiSsid, AppConfig::kWifiPassword);

  const uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startMs >= AppConfig::kWifiConnectTimeoutMs) {
      setFatalState(state, "WIFI TIMEOUT");
      return;
    }
    delay(250);
  }

  state.wifiStatus = "OK";
  state.ipAddress = WiFi.localIP().toString();
  state.lastEvent = "WIFI READY";
  requestRender(state);
}

void PrinterComm::ensureMqtt(AppState& state) {
  if (mqttClient_.connected()) {
    return;
  }

  int lastError = 0;
  for (uint8_t retry = 0; retry < AppConfig::kMqttMaxRetries && !mqttClient_.connected(); ++retry) {
    state.mqttStatus = "TRY";
    state.lastEvent = "MQTT " + String(retry + 1) + "/" + String(AppConfig::kMqttMaxRetries);
    requestRender(state);

    if (mqttClient_.connect(mqttClientId_, AppConfig::kPrinterUser, AppConfig::kPrinterPassword)) {
      state.mqttStatus = "OK";
      state.lastEvent = "MQTT READY";
      requestRender(state);

      if (mqttClient_.subscribe(topicSubscribe_)) {
        state.lastEvent = "SUB OK";
      } else {
        state.lastEvent = "SUB FAIL";
      }
      requestRender(state);

      startHoming(state);
      queryPosition(state);
      return;
    }

    lastError = mqttClient_.state();
    state.mqttStatus = mqttErrorLabel(lastError);
    state.lastEvent = state.mqttStatus;
    requestRender(state);
    delay(AppConfig::kMqttRetryDelayMs);
  }

  setFatalState(state, "MQTT CODE " + String(lastError));
}

void PrinterComm::onMessage(char* topic, byte* payload, unsigned int length) {
  if (state_ == nullptr) {
    return;
  }

  String message;
  message.reserve(length + 1);
  for (unsigned int i = 0; i < length; ++i) {
    message += static_cast<char>(payload[i]);
  }

  Serial.print("Message received on topic: ");
  Serial.println(topic);
  Serial.println("Message content: " + message);

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, message);
  if (error) {
    state_->lastEvent = "JSON ERR";
    requestRender(*state_);
    return;
  }

  JsonVariantConst printValue = doc["print"];
  if (printValue.isNull() || !printValue.is<JsonObjectConst>()) {
    return;
  }

  JsonObjectConst print = printValue.as<JsonObjectConst>();
  JsonVariantConst command = print["command"];
  if (!command.is<const char*>() || strcmp(command.as<const char*>(), "push_status") != 0) {
    return;
  }

  state_->lastEvent = "PUSH STATUS";

  if (!print["bed_temper"].isNull()) {
    state_->bedTemp = formatTemperature(print["bed_temper"].as<float>());
  }
  if (!print["nozzle_temper"].isNull()) {
    state_->nozzleTemp = formatTemperature(print["nozzle_temper"].as<float>());
  }
  if (!print["wifi_signal"].isNull()) {
    state_->printerWifi = print["wifi_signal"].as<String>();
  }
  if (!print["sequence_id"].isNull()) {
    state_->sequenceId = print["sequence_id"].as<String>();
  }
  if (!print["homing_status"].isNull()) {
    state_->homingStatus = print["homing_status"].as<String>();
  }
  if (!print["gcode_state"].isNull()) {
    state_->printState = print["gcode_state"].as<String>();
  }
  if (!print["mc_percent"].isNull()) {
    state_->progress = formatPercent(print["mc_percent"].as<int>());
  }
  if (!print["layer_num"].isNull()) {
    String layerText = String(print["layer_num"].as<int>());
    if (!print["total_layer_num"].isNull()) {
      layerText += "/";
      layerText += String(print["total_layer_num"].as<int>());
    }
    state_->layer = layerText;
  }

  requestRender(*state_);
}

void PrinterComm::startHoming(AppState& state) {
  JsonDocument doc;
  doc["command"] = "G_code";
  doc["param"] = "G28";
  doc["sequence_id"] = String(millis());
  doc["user_id"] = AppConfig::kPrinterSerial;
  publishJson(state, doc, "HOME SENT", "HOME FAIL");
}

void PrinterComm::queryPosition(AppState& state) {
  JsonDocument doc;
  doc["command"] = "get_position";
  doc["sequence_id"] = String(millis());
  doc["user_id"] = AppConfig::kPrinterSerial;
  publishJson(state, doc, "POS SENT", "POS FAIL");
}

bool PrinterComm::publishJson(AppState& state, JsonDocument& doc, const char* okEvent, const char* failEvent) {
  String payload;
  serializeJson(doc, payload);

  const bool published = mqttClient_.publish(topicPublish_, payload.c_str(), true);
  state.lastEvent = published ? okEvent : failEvent;
  requestRender(state);
  return published;
}

void PrinterComm::setFatalState(AppState& state, const String& reason) {
  state.mqttStatus = "STOP";
  state.lastEvent = "FATAL";
  state.errorReason = reason;
  state.halted = true;
  requestRender(state);
  Serial.println("Fatal error: " + reason);
}

void PrinterComm::requestRender(AppState& state) {
  state.displayDirty = true;
}
