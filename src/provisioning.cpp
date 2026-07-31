#include "provisioning.h"

#include <Preferences.h>
#include <WiFiManager.h>

namespace provisioning {
namespace {

constexpr int PIN_BOOT = 0;
constexpr uint32_t HOLD_MS = 3000;
constexpr uint16_t PORTAL_TIMEOUT_S = 300;

Preferences prefs;
uint32_t pressedSince = 0;

// Preferences logs an ERROR for every string key it cannot find, so reading the
// defaults on a fresh device makes a perfectly normal first boot look like four
// failures. Ask before reading.
String readString(const char *key) {
  return prefs.isKey(key) ? prefs.getString(key) : String();
}

void load(Config &out) {
  out.mqttHost = readString("host");
  out.mqttPort = prefs.getUShort("port", 1883);
  out.mqttUser = readString("user");
  out.mqttPass = readString("pass");
  out.otaPass = readString("ota");
}

void store(const Config &c) {
  prefs.putString("host", c.mqttHost);
  prefs.putUShort("port", c.mqttPort);
  prefs.putString("user", c.mqttUser);
  prefs.putString("pass", c.mqttPass);
  prefs.putString("ota", c.otaPass);
}

// Runs the portal (or a plain reconnect) and writes back whatever was entered.
bool runPortal(Config &out, bool forced) {
  char port[8];
  snprintf(port, sizeof(port), "%u", out.mqttPort);

  WiFiManagerParameter p_host("host", "MQTT host", out.mqttHost.c_str(), 64);
  WiFiManagerParameter p_port("port", "MQTT port", port, 6);
  WiFiManagerParameter p_user("user", "MQTT user", out.mqttUser.c_str(), 32);
  WiFiManagerParameter p_pass("pass", "MQTT password", out.mqttPass.c_str(), 96);
  WiFiManagerParameter p_ota("ota", "OTA password", out.otaPass.c_str(), 32);

  WiFiManager wm;
  wm.addParameter(&p_host);
  wm.addParameter(&p_port);
  wm.addParameter(&p_user);
  wm.addParameter(&p_pass);
  wm.addParameter(&p_ota);
  wm.setConfigPortalTimeout(PORTAL_TIMEOUT_S);
  wm.setHostname("bordbar");

  auto collect = [&]() {
    out.mqttHost = p_host.getValue();
    out.mqttPort = static_cast<uint16_t>(atoi(p_port.getValue()));
    out.mqttUser = p_user.getValue();
    out.mqttPass = p_pass.getValue();
    out.otaPass = p_ota.getValue();
    store(out);
    Serial.printf("stored MQTT %s:%u as %s (OTA %s)\n", out.mqttHost.c_str(), out.mqttPort,
                  out.mqttUser.c_str(), out.otaPass.isEmpty() ? "disabled" : "enabled");
  };

  // Persist as soon as the form is submitted. Saving only after the portal
  // returns loses everything when the user submits on the parameter page and
  // the portal then runs into its timeout instead of ending on a WiFi save.
  wm.setSaveParamsCallback(collect);
  // End the portal once credentials are entered, even if the join fails, so a
  // typo in the WiFi password does not discard the MQTT fields as well.
  wm.setBreakAfterConfig(true);

  bool portal = forced || out.mqttHost.isEmpty();
  bool connected = portal ? wm.startConfigPortal("bordbar-setup") : wm.autoConnect("bordbar-setup");
  // Second chance for the case where the portal ends without firing the
  // callback at all; a plain reconnect has nothing new to store.
  if (portal) collect();
  if (!connected) {
    Serial.println("portal ended without a WiFi connection — settings were still stored");
    return false;
  }

  Serial.printf("WiFi %s | MQTT %s:%u as %s\n", WiFi.localIP().toString().c_str(),
                out.mqttHost.c_str(), out.mqttPort, out.mqttUser.c_str());
  return true;
}

}  // namespace

bool begin(Config &out) {
  prefs.begin("bordbar-net", false);
  pinMode(PIN_BOOT, INPUT_PULLUP);
  load(out);
  return runPortal(out, false);
}

bool portalButtonHeld() {
  if (digitalRead(PIN_BOOT) == HIGH) {
    if (pressedSince != 0) Serial.println("BOOT released early — hold the full 3s");
    pressedSince = 0;
    return false;
  }
  uint32_t now = millis();
  if (pressedSince == 0) {
    pressedSince = now;
    // Printed on press, not only on success: without it a button that never
    // reads LOW is indistinguishable from one that is released too early.
    Serial.println("BOOT pressed — keep holding for 3s");
    return false;
  }
  return now - pressedSince >= HOLD_MS;
}

void openPortal() {
  Serial.println("BOOT held — opening configuration portal on AP bordbar-setup");
  Config cfg;
  load(cfg);
  runPortal(cfg, true);
  ESP.restart();
}

}  // namespace provisioning
