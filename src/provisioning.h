// WiFi and MQTT credentials live in NVS, entered once through a captive portal.
// Nothing is compiled into the binary, so a release .bin can be published and a
// password can be rotated without reflashing.
#pragma once

#include <Arduino.h>

namespace provisioning {

struct Config {
  String mqttHost;
  uint16_t mqttPort = 1883;
  String mqttUser;
  String mqttPass;
  String otaPass;  // empty disables OTA
};

// Connects to WiFi, opening the "bordbar-setup" portal when no configuration
// exists yet or when the BOOT button is held during reset. Returns false if the
// portal timed out without input — the caller should reboot.
bool begin(Config &out);

}  // namespace provisioning
