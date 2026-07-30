// CC1101 in asynchronous serial OOK mode, driving a PT2262 waveform from the RMT peripheral.
#pragma once

#include <stdint.h>

namespace rf {

// Resets and configures the CC1101 and claims GDO0 as an RMT output.
// Returns false if the chip does not answer on SPI.
bool begin();

// Transmits one 24-bit code as the original remote does: 7 repeats, each
// terminated by the sync gap. Blocks for roughly 360 ms.
void send(uint32_t code);

}  // namespace rf
