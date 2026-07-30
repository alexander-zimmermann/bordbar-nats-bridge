#include "state.h"

#include <Preferences.h>

namespace state {
namespace {

Preferences prefs;
bool assumed = false;

}  // namespace

void begin() {
  prefs.begin("bordbar", false);
  assumed = prefs.getBool("power", false);
}

bool power() {
  return assumed;
}

void setPower(bool on) {
  if (on == assumed) return;
  assumed = on;
  prefs.putBool("power", on);
}

}  // namespace state
