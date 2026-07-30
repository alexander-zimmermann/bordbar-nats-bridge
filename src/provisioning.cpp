#include "provisioning.h"

#include <Preferences.h>
#include <WiFiManager.h>

namespace provisioning {
namespace {

// Holding BOOT (GPIO0) low during reset forces the portal open again, which is
// the only way back in once the cabinet is closed and the old WiFi is gone.
constexpr int PIN_BOOT = 0;
constexpr uint16_t PORTAL_TIMEOUT_S = 300;

Preferences prefs;

bool portalRequested() {
  pinMode(PIN_BOOT, INPUT_PULLUP);
  delay(10);
  return digitalRead(PIN_BOOT) == LOW;
}

// Preferences logs an ERROR for every string key it cannot find, so reading the
// defaults on a fresh device makes a perfectly normal first boot look like four
// failures. Ask before reading.
String readString(const char *key) {
  return prefs.isKey(key) ? prefs.getString(key) : String();
}

}  // namespace

bool begin(Config &out) {
  prefs.begin("bordbar-net", false);
  out.mqttHost = readString("host");
  out.mqttPort = prefs.getUShort("port", 1883);
  out.mqttUser = readString("user");
  out.mqttPass = readString("pass");
  out.otaPass = readString("ota");

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

  bool forced = portalRequested();
  if (forced) Serial.println("BOOT held during reset — opening configuration portal");

  bool connected = (forced || out.mqttHost.isEmpty()) ? wm.startConfigPortal("bordbar-setup")
                                                      : wm.autoConnect("bordbar-setup");
  if (!connected) {
    Serial.println("portal timed out without a usable configuration");
    return false;
  }

  // WiFiManager keeps the WiFi credentials itself; the MQTT ones are ours.
  out.mqttHost = p_host.getValue();
  out.mqttPort = static_cast<uint16_t>(atoi(p_port.getValue()));
  out.mqttUser = p_user.getValue();
  out.mqttPass = p_pass.getValue();
  out.otaPass = p_ota.getValue();

  prefs.putString("host", out.mqttHost);
  prefs.putUShort("port", out.mqttPort);
  prefs.putString("user", out.mqttUser);
  prefs.putString("pass", out.mqttPass);
  prefs.putString("ota", out.otaPass);

  Serial.printf("WiFi %s | MQTT %s:%u as %s\n", WiFi.localIP().toString().c_str(),
                out.mqttHost.c_str(), out.mqttPort, out.mqttUser.c_str());
  return true;
}

}  // namespace provisioning
