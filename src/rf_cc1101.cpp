/*
 * Bordbar LED transmitter: CC1101 at 433.92 MHz, PT2262 protocol, fixed codes.
 *
 * The chip runs in asynchronous serial OOK mode (PKTCTRL0 = 0x32): it only
 * produces the carrier, and the level on GDO0 gates it on and off — GDO0 behaves
 * like the data pin of an FS1000A. The packet engine stays out of the way, which
 * is necessary because PT2262 has neither preamble nor sync nor CRC in the sense
 * the chip expects.
 *
 * The pulse train comes from the RMT peripheral rather than delayMicroseconds().
 * A software-timed burst would have to run with interrupts disabled for ~360 ms
 * to survive WiFi activity, which starves the WiFi stack; RMT clocks the waveform
 * in hardware and leaves interrupts untouched.
 *
 * Timings are from our own measurement with `rtl_433 -A` (2026-07-28), not from
 * library defaults — that is why the transmitter matches the original exactly.
 */
#include "rf_cc1101.h"

#include <Arduino.h>
#include <SPI.h>
#include <esp32-hal-rmt.h>

namespace rf {
namespace {

// ---- Wiring (VSPI) --------------------------------------------------------
constexpr int PIN_SCK = 18;
constexpr int PIN_MISO = 19;
constexpr int PIN_MOSI = 23;
constexpr int PIN_CS = 5;
constexpr int PIN_GDO0 = 17;  // in async mode: data input, gates the carrier

// ---- CC1101 registers -----------------------------------------------------
enum : uint8_t {
  IOCFG2 = 0x00,
  IOCFG0 = 0x02,
  FIFOTHR = 0x03,
  PKTLEN = 0x06,
  PKTCTRL1 = 0x07,
  PKTCTRL0 = 0x08,
  FSCTRL1 = 0x0B,
  FSCTRL0 = 0x0C,
  FREQ2 = 0x0D,
  FREQ1 = 0x0E,
  FREQ0 = 0x0F,
  MDMCFG4 = 0x10,
  MDMCFG3 = 0x11,
  MDMCFG2 = 0x12,
  MDMCFG1 = 0x13,
  MDMCFG0 = 0x14,
  DEVIATN = 0x15,
  MCSM1 = 0x17,
  MCSM0 = 0x18,
  FOCCFG = 0x19,
  BSCFG = 0x1A,
  AGCCTRL2 = 0x1B,
  AGCCTRL1 = 0x1C,
  AGCCTRL0 = 0x1D,
  FREND1 = 0x21,
  FREND0 = 0x22,
  FSCAL3 = 0x23,
  FSCAL2 = 0x24,
  FSCAL1 = 0x25,
  FSCAL0 = 0x26,
  TEST2 = 0x2C,
  TEST1 = 0x2D,
  TEST0 = 0x2E,
  PATABLE = 0x3E,
};
enum : uint8_t { SRES = 0x30, SCAL = 0x33, STX = 0x35, SIDLE = 0x36 };
enum : uint8_t { PARTNUM = 0x30, VERSION = 0x31 };

SPISettings spiCfg(4000000, MSBFIRST, SPI_MODE0);

// clang-format off
const uint8_t REGS[][2] = {
  {IOCFG2,   0x2E},
  {IOCFG0,   0x2E},  // use GDO0 as an input -> disable its output function
  {FIFOTHR,  0x47},
  {PKTCTRL0, 0x32},  // asynchronous serial mode, no packet engine
  {PKTCTRL1, 0x00},
  {PKTLEN,   0xFF},
  {FSCTRL1,  0x06}, {FSCTRL0, 0x00},
  {FREQ2,    0x10}, {FREQ1, 0xB0}, {FREQ0, 0x71},   // 433.9198 MHz
  {MDMCFG4,  0xF6},  // narrow channel bandwidth + DRATE_E=6
  {MDMCFG3,  0x63},  // ~2200 baud (async mode: internal filters only)
  {MDMCFG2,  0x30},  // ASK/OOK, no Manchester, no sync
  {MDMCFG1,  0x22}, {MDMCFG0, 0xF8},
  {DEVIATN,  0x15},  // irrelevant for OOK
  {MCSM1,    0x30}, {MCSM0, 0x18},
  {FOCCFG,   0x16}, {BSCFG, 0x6C},
  {AGCCTRL2, 0x03}, {AGCCTRL1, 0x00}, {AGCCTRL0, 0x91},
  {FREND1,   0x56}, {FREND0, 0x11},  // FREND0: PATABLE index 1 for OOK high
  {FSCAL3,   0xE9}, {FSCAL2, 0x2A}, {FSCAL1, 0x00}, {FSCAL0, 0x1F},
  {TEST2,    0x81}, {TEST1, 0x35}, {TEST0, 0x09},
};
// clang-format on

// ---- Protocol: measured timings in microseconds ---------------------------
constexpr uint16_t ONE_HIGH = 1236;
constexpr uint16_t ONE_LOW = 376;
constexpr uint16_t ZERO_HIGH = 460;
constexpr uint16_t ZERO_LOW = 1164;
constexpr uint16_t SYNC_HIGH = 460;
constexpr uint16_t SYNC_LOW = 11932;

constexpr uint8_t CODE_BITS = 24;
constexpr uint8_t REPEATS = 7;  // the original remote repeats this often too

// One rmt_data_t holds a high/low pair, so one item encodes one bit. 24 bits plus
// the trailing sync gap, plus a zero item that tells RMT the burst ends here.
constexpr size_t ITEMS_PER_BURST = CODE_BITS + 2;

rmt_obj_t *rmt = nullptr;

void csLow() {
  digitalWrite(PIN_CS, LOW);
  uint32_t t0 = micros();
  while (digitalRead(PIN_MISO) && micros() - t0 < 2000) {
  }
}
void csHigh() {
  digitalWrite(PIN_CS, HIGH);
}

void strobe(uint8_t cmd) {
  SPI.beginTransaction(spiCfg);
  csLow();
  SPI.transfer(cmd);
  csHigh();
  SPI.endTransaction();
}

void writeReg(uint8_t addr, uint8_t value) {
  SPI.beginTransaction(spiCfg);
  csLow();
  SPI.transfer(addr);
  SPI.transfer(value);
  csHigh();
  SPI.endTransaction();
}

uint8_t readStatus(uint8_t addr) {
  SPI.beginTransaction(spiCfg);
  csLow();
  SPI.transfer(addr | 0xC0);
  uint8_t value = SPI.transfer(0);
  csHigh();
  SPI.endTransaction();
  return value;
}

void burstWrite(uint8_t addr, const uint8_t *bytes, uint8_t n) {
  SPI.beginTransaction(spiCfg);
  csLow();
  SPI.transfer(addr | 0x40);
  for (uint8_t i = 0; i < n; i++) SPI.transfer(bytes[i]);
  csHigh();
  SPI.endTransaction();
}

void configure() {
  strobe(SRES);
  delay(10);
  for (auto &r : REGS) writeReg(r[0], r[1]);
  // In OOK the chip alternates between PATABLE[0] and PATABLE[1]:
  // index 0 = carrier off, index 1 = full power.
  uint8_t pa[] = {0x00, 0xC0};
  burstWrite(PATABLE, pa, 2);
  strobe(SCAL);
  delay(5);
}

void fillItem(rmt_data_t &item, uint16_t high, uint16_t low) {
  item.level0 = 1;
  item.duration0 = high;
  item.level1 = 0;
  item.duration1 = low;
}

}  // namespace

bool begin() {
  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);

  strobe(SRES);
  delay(10);
  uint8_t version = readStatus(VERSION);
  Serial.printf("CC1101 PARTNUM=0x%02X VERSION=0x%02X\n", readStatus(PARTNUM), version);
  if (version == 0x00 || version == 0xFF) {
    Serial.println("CC1101 does not answer — check wiring (VCC on 3V3, not 5V)");
    return false;
  }

  configure();

  rmt = rmtInit(PIN_GDO0, RMT_TX_MODE, RMT_MEM_64);
  if (rmt == nullptr) {
    Serial.println("RMT channel could not be claimed for GDO0");
    return false;
  }
  // Durations are in microseconds; the peripheral divides the 80 MHz APB clock,
  // so 1000 ns lands exactly on a divider of 80.
  float tick = rmtSetTick(rmt, 1000.0f);
  Serial.printf("RMT tick %.1f ns | 433.92 MHz OOK async | PT2262 | house code 57635\n", tick);
  return true;
}

void send(uint32_t code) {
  if (rmt == nullptr) {
    Serial.println("send() called before rf::begin() succeeded");
    return;
  }

  rmt_data_t items[ITEMS_PER_BURST];
  for (int8_t i = CODE_BITS - 1; i >= 0; i--) {
    rmt_data_t &item = items[CODE_BITS - 1 - i];
    if ((code >> i) & 1) {
      fillItem(item, ONE_HIGH, ONE_LOW);
    } else {
      fillItem(item, ZERO_HIGH, ZERO_LOW);
    }
  }
  fillItem(items[CODE_BITS], SYNC_HIGH, SYNC_LOW);  // sync closes every packet
  items[CODE_BITS + 1].val = 0;                     // end marker

  strobe(STX);  // arm the carrier, GDO0 now modulates it
  delayMicroseconds(500);
  for (uint8_t r = 0; r < REPEATS; r++) {
    rmtWriteBlocking(rmt, items, ITEMS_PER_BURST);
  }
  strobe(SIDLE);

  Serial.printf("sent 0x%06X (%dx)\n", code, REPEATS);
}

}  // namespace rf
