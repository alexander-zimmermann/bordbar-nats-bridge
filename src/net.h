// MQTT client against the NATS MQTT gateway, plus OTA.
//
// Topics map 1:1 onto NATS subjects (the gateway turns '/' into '.'):
//   bordbar/state            -> bordbar.state
//   bordbar/availability     -> bordbar.availability
//   bordbar/command/<action> -> bordbar.command.<action>
#pragma once

#include "provisioning.h"

namespace net {

// `value` is 1/0 for booleans and +1/-1 for step commands. `has_value` is false
// when the payload carried none, as for the reset command.
using CommandHandler = void (*)(const char *action, int value, bool has_value);
using ConnectHandler = void (*)();

void begin(const provisioning::Config &cfg, CommandHandler on_command, ConnectHandler on_connect);

// Pumps MQTT and OTA, reconnecting with backoff. Call from loop().
void loop();

// True while the MQTT session is up; publishes are silently lost otherwise.
bool connected();

// Retained, so a restarting knx-nats-bridge can seed the status GA.
void publishPower(bool on);

}  // namespace net
