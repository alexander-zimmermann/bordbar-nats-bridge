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

// Connects to WiFi, opening the "bordbar-setup" portal when nothing is stored
// yet. Returns false if the portal timed out without input — the caller should
// reboot.
bool begin(Config &out);

// True once BOOT (GPIO 0) has been held for three seconds. Deliberately polled
// from loop() instead of sampled at startup: GPIO 0 is a strapping pin, so
// holding it during reset drops the chip into the ROM download loader and the
// application never runs.
bool portalButtonHeld();

// Opens the portal with the stored values prefilled, saves what was entered and
// restarts. Does not return.
void openPortal();

}  // namespace provisioning
