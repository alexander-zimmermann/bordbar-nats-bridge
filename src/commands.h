// Measured 24-bit codes of the original remote (PT2262, house code 57635 / 0xE123).
// Captured with `rtl_433 -A` on 2026-07-28; see README for how to re-measure.
#pragma once

#include <stdint.h>

// House code occupies the upper 16 bits, the command the lower 8.
// The command values follow no recognisable system — they are measured, not derived.
enum : uint32_t {
  CODE_TOGGLE = 0xE12301,  // single button for on and off
  CODE_COLOR_UP = 0xE1230A,
  CODE_COLOR_DOWN = 0xE1230D,
  CODE_BRIGHT_UP = 0xE1230C,
  CODE_BRIGHT_DOWN = 0xE1230F,
  CODE_MODE_UP = 0xE12305,
  CODE_MODE_DOWN = 0xE1230B,
  CODE_SPEED_UP = 0xE12309,
  CODE_SPEED_DOWN = 0xE12307,
};
