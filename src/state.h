// Assumed light state. The LED controller has no feedback channel and its only
// power button is a toggle, so all this module can do is remember what we sent.
// Kept in NVS so a reboot does not silently invert the assumption.
#pragma once

namespace state {

void begin();
bool power();

// Stores the new assumption; writes to NVS only when it actually changed.
void setPower(bool on);

}  // namespace state
