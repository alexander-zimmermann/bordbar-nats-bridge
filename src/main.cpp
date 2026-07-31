/*
 * Bordbar LED bridge: NATS (via the MQTT gateway) -> 433.92 MHz PT2262.
 *
 * The LED controller is write-only and its power button is a toggle, so the
 * on/off state here is an assumption, not a measurement — see state.h and the
 * reset command below.
 */
#include <Arduino.h>

#include "commands.h"
#include "net.h"
#include "provisioning.h"
#include "rf_cc1101.h"
#include "state.h"

namespace {

constexpr char FW_VERSION[] = "0.1.0";

provisioning::Config config;

void onCommand(const char *action, int value, bool has_value) {
  Serial.printf("command %s value=%d has_value=%d\n", action, value, has_value);

  if (strcmp(action, "power") == 0) {
    if (!has_value) return;
    bool want = value != 0;
    // The remote has no discrete on and off, only a toggle — so send nothing
    // when we already believe the light is in the requested state.
    if (want != state::power()) {
      rf::send(CODE_TOGGLE);
      state::setPower(want);
      net::publishPower(want);
    }
    return;
  }

  if (strcmp(action, "reset") == 0) {
    // Resynchronises the assumption after someone used the physical remote:
    // switch the light off by hand, then trigger this. Also a way to reboot a
    // wedged ESP32 without opening the cabinet.
    state::setPower(false);
    Serial.println("reset requested — assuming light is off and restarting");
    delay(200);
    ESP.restart();
    return;
  }

  if (!has_value || value == 0) return;
  bool up = value > 0;

  if (strcmp(action, "brightness") == 0) {
    rf::send(up ? CODE_BRIGHT_UP : CODE_BRIGHT_DOWN);
  } else if (strcmp(action, "color") == 0) {
    rf::send(up ? CODE_COLOR_UP : CODE_COLOR_DOWN);
  } else if (strcmp(action, "mode") == 0) {
    rf::send(up ? CODE_MODE_UP : CODE_MODE_DOWN);
  } else if (strcmp(action, "speed") == 0) {
    rf::send(up ? CODE_SPEED_UP : CODE_SPEED_DOWN);
  } else {
    Serial.printf("unknown command %s\n", action);
  }
}

void onConnect() {
  net::publishPower(state::power());
}

// Serial diagnostics, keys 1-9. Works without network, MQTT or KNX, which is
// the only way to tell a dead transmitter from a dead bus on a device that
// never reports back. Key 1 flips the assumption like any other toggle does.
void handleSerial() {
  if (!Serial.available()) return;
  char key = Serial.read();

  // Same portal as the BOOT long-press, but reachable while the board is on a
  // USB cable — no dependency on the button reading correctly.
  if (key == 'p') {
    provisioning::openPortal();
    return;
  }

  if (key == '1') {
    rf::send(CODE_TOGGLE);
    bool now = !state::power();
    state::setPower(now);
    net::publishPower(now);
    return;
  }

  switch (key) {
    case '2':
      rf::send(CODE_BRIGHT_UP);
      break;
    case '3':
      rf::send(CODE_BRIGHT_DOWN);
      break;
    case '4':
      rf::send(CODE_COLOR_UP);
      break;
    case '5':
      rf::send(CODE_COLOR_DOWN);
      break;
    case '6':
      rf::send(CODE_MODE_UP);
      break;
    case '7':
      rf::send(CODE_MODE_DOWN);
      break;
    case '8':
      rf::send(CODE_SPEED_UP);
      break;
    case '9':
      rf::send(CODE_SPEED_DOWN);
      break;
    default:
      break;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.printf("\nbordbar-nats-bridge %s\n", FW_VERSION);

  state::begin();
  Serial.printf("assumed light state: %s\n", state::power() ? "on" : "off");

  // A missing CC1101 is a hardware fault that rebooting will not fix, so carry
  // on and let rf::send() complain rather than loop on restart.
  if (!rf::begin()) Serial.println("continuing without a working transmitter");

  if (!provisioning::begin(config)) {
    Serial.println("no network configuration — restarting");
    delay(1000);
    ESP.restart();
  }

  net::begin(config, onCommand, onConnect);
  Serial.println("serial diagnostics: 1=on/off 2/3=bright 4/5=color 6/7=mode 8/9=speed");
  Serial.println("press p (or hold BOOT for 3s) to reopen the configuration portal");
}

void loop() {
  net::loop();
  handleSerial();
  if (provisioning::portalButtonHeld()) provisioning::openPortal();
}
