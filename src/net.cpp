#include "net.h"

#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <WiFi.h>

namespace net {
namespace {

constexpr char TOPIC_STATE[] = "bordbar/state";
constexpr char TOPIC_AVAILABILITY[] = "bordbar/availability";
constexpr char TOPIC_COMMANDS[] = "bordbar/command/#";
constexpr char COMMAND_PREFIX[] = "bordbar/command/";
constexpr char CLIENT_ID[] = "bordbar";

// The last will is what makes a dead ESP32 visible on the KNX bus: NATS
// publishes it on our behalf and the writer rule drops the availability GA.
constexpr char PAYLOAD_ONLINE[] = "{\"online\":true}";
constexpr char PAYLOAD_OFFLINE[] = "{\"online\":false}";

constexpr uint32_t BACKOFF_MIN_MS = 1000;
constexpr uint32_t BACKOFF_MAX_MS = 60000;

WiFiClient wifi;
PubSubClient mqtt(wifi);
provisioning::Config config;
CommandHandler commandHandler = nullptr;
ConnectHandler connectHandler = nullptr;

uint32_t backoffMs = BACKOFF_MIN_MS;
uint32_t nextAttemptAt = 0;

void onMessage(char *topic, byte *payload, unsigned int length) {
  size_t prefixLen = strlen(COMMAND_PREFIX);
  if (strncmp(topic, COMMAND_PREFIX, prefixLen) != 0) return;
  const char *action = topic + prefixLen;

  int value = 0;
  bool hasValue = false;
  JsonDocument doc;
  if (deserializeJson(doc, payload, length) == DeserializationError::Ok) {
    JsonVariantConst v = doc["value"];
    if (v.is<bool>()) {
      value = v.as<bool>() ? 1 : 0;
      hasValue = true;
    } else if (v.is<int>()) {
      value = v.as<int>();
      hasValue = true;
    }
  }

  if (commandHandler != nullptr) commandHandler(action, value, hasValue);
}

void connect() {
  Serial.printf("connecting to MQTT %s:%u\n", config.mqttHost.c_str(), config.mqttPort);
  bool ok = mqtt.connect(CLIENT_ID, config.mqttUser.c_str(), config.mqttPass.c_str(),
                         TOPIC_AVAILABILITY, 0, true, PAYLOAD_OFFLINE);
  if (!ok) {
    backoffMs = min(backoffMs * 2, BACKOFF_MAX_MS);
    nextAttemptAt = millis() + backoffMs;
    Serial.printf("MQTT connect failed (state %d), retrying in %u ms\n", mqtt.state(), backoffMs);
    return;
  }

  backoffMs = BACKOFF_MIN_MS;
  mqtt.publish(TOPIC_AVAILABILITY, PAYLOAD_ONLINE, true);
  mqtt.subscribe(TOPIC_COMMANDS);
  Serial.println("MQTT connected");
  if (connectHandler != nullptr) connectHandler();
}

}  // namespace

void begin(const provisioning::Config &cfg, CommandHandler on_command, ConnectHandler on_connect) {
  config = cfg;
  commandHandler = on_command;
  connectHandler = on_connect;

  mqtt.setServer(config.mqttHost.c_str(), config.mqttPort);
  mqtt.setCallback(onMessage);

  ArduinoOTA.setHostname("bordbar");
  if (config.otaPass.isEmpty()) {
    Serial.println("no OTA password configured — OTA stays disabled");
  } else {
    ArduinoOTA.setPassword(config.otaPass.c_str());
    ArduinoOTA.begin();
  }
}

void loop() {
  if (!config.otaPass.isEmpty()) ArduinoOTA.handle();

  if (mqtt.connected()) {
    mqtt.loop();
    return;
  }
  if (WiFi.status() != WL_CONNECTED) return;
  if (millis() < nextAttemptAt) return;
  connect();
}

void publishPower(bool on) {
  mqtt.publish(TOPIC_STATE, on ? "{\"power\":true}" : "{\"power\":false}", true);
}

}  // namespace net
