#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "AppState.h"

class PrinterComm {
 public:
  PrinterComm();

  void begin(AppState& state);
  void tick(AppState& state);

 private:
  static void mqttCallback(char* topic, byte* payload, unsigned int length);

  void setupWiFi(AppState& state);
  void ensureMqtt(AppState& state);
  void onMessage(char* topic, byte* payload, unsigned int length);
  void startHoming(AppState& state);
  void queryPosition(AppState& state);
  bool publishJson(AppState& state, JsonDocument& doc, const char* okEvent, const char* failEvent);
  void setFatalState(AppState& state, const String& reason);
  void requestRender(AppState& state);

  static PrinterComm* instance_;

  WiFiClientSecure tlsClient_;
  PubSubClient mqttClient_;
  AppState* state_ = nullptr;
  char topicSubscribe_[96] = {};
  char topicPublish_[96] = {};
  char mqttClientId_[64] = {};
};
