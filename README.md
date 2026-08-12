# bordbar-nats-bridge

ESP32 firmware that drives the LED lighting of a bar cabinet ("Bordbar") over
433.92 MHz, controlled from NATS through the NATS MQTT gateway.

The light is a cheap RGB controller with a fixed-code PT2262 remote. There is no
receiver in it and no feedback channel, so this bridge is one-way: it reproduces
the remote's transmissions and *remembers* what it sent.

## What it does

- Subscribes to `bordbar.command.*` and transmits the matching remote code.
- Publishes its assumed on/off state on `bordbar.state`.
- Publishes reachability on `bordbar.availability`, backed by an MQTT
  last will, so a dead device is visible instead of silently swallowing commands.

## Status

Phase 1 of the Lares integration: firmware and NATS wiring. The KNX/Basalte side
lives in the `lares` repository (group addresses 4/2/60–68).

## Hardware

ESP32 dev board plus a CC1101 on 433.92 MHz with an SMA antenna, wired to VSPI.
The radio sits on an Ebyte MBL test kit with an **E07-400M10S** module; the pin
numbers below are the MBL carrier board's, not the module's own pads:

| MBL pin | Signal | ESP32 |
|---|---|---|
| 3 | 3V3 (**not** 5V) | 3V3 |
| 5 | GND | GND |
| 12 | SCLK | GPIO 18 |
| 10 | MISO | GPIO 19 |
| 11 | MOSI | GPIO 23 |
| 13 | NSS | GPIO 5 |
| 9 | **GDO0** | GPIO 17 |

Only the module power jumper (1↔2) is fitted. The MCU supply (3↔4) and the mode
caps on M0/M1 stay off, otherwise the carrier board's STM8 drives the same lines.

Pin 9 is the trap. The MBL manual calls it "PC0 / Module reset pin" because the
carrier board serves thirteen different modules and that line is `NRST` for the
Semtech ones. A CC1101 has no reset pin — it is reset with the `SRES` strobe over
SPI — and on the E07 footprint the same position carries GDO0. M0 and M1 are not
it, and TXD/RXD reach only the USB bridge.

A wrong GDO0 still looks healthy from the outside: SPI answers with
`VERSION=0x14` and `rf::send()` prints `sent 0x…`, but in asynchronous OOK mode
only GDO0 keys the carrier, so nothing leaves the antenna. `rtl_433 -f 433.92M -A`
while pressing `1` on the serial console is the check that proves the radio.

A plain 8-pin CC1101 board works too: SCK, MISO, MOSI, CSN and GDO0 go to the
same ESP32 pins, VCC to 3V3.

## Protocol

433.92 MHz, OOK, PT2262 fixed code. 24 bits — house code 57635 (0xE123) in the
upper 16, command in the lower 8 — repeated 7 times, each repeat closed by the
sync gap. The command values follow no recognisable system; they are measured.

| Button | cmd | Code |
|---|---|---|
| On/Off (toggle) | 1 | `0xE12301` |
| Mode + | 5 | `0xE12305` |
| Speed − | 7 | `0xE12307` |
| Speed + | 9 | `0xE12309` |
| Color + | 10 | `0xE1230A` |
| Mode − | 11 | `0xE1230B` |
| Bright + | 12 | `0xE1230C` |
| Color − | 13 | `0xE1230D` |
| Bright − | 15 | `0xE1230F` |

Pulse timings, measured on 2026-07-28 and used verbatim by the transmitter:

| Symbol | High | Low |
|---|---|---|
| `1` | 1236 µs | 376 µs |
| `0` | 460 µs | 1164 µs |
| sync | 460 µs | 11932 µs |

The CC1101 runs in asynchronous serial OOK mode (`PKTCTRL0 = 0x32`): it only
generates the carrier, and the level on GDO0 gates it — GDO0 behaves like the data
pin of an FS1000A. The packet engine stays out of the way, which is necessary
because PT2262 has neither preamble nor sync nor CRC in the sense the chip expects.

The waveform comes from the **RMT peripheral**, not from `delayMicroseconds()`. A
software-timed burst would have to run with interrupts disabled for roughly 360 ms
per command, which starves the WiFi stack; RMT clocks it in hardware. One
`rmt_data_t` holds a high/low pair, so one item encodes one bit.

### Re-measuring the protocol

Only needed for a replacement remote or a second unit. With an RTL-SDR:

```sh
# Raw pulse analysis — gives the timings above
rtl_433 -f 433.92M -A

# Decoded values while pressing one button
rtl_433 -f 433.92M -F json -T 6
```

Take the most frequent `Generic-Remote` result as the real one; single
outliers are reception errors. The 24-bit value is house code × 256 + command.
Put new codes into [src/commands.h](src/commands.h) — that file is the normative
source, there is no separate data file.

## NATS subjects

The MQTT gateway maps `/` to `.`, so the firmware's topics are the subjects.

| Subject | Payload | Meaning |
|---|---|---|
| `bordbar.command.power` | `{"value": true\|false}` | on/off; sends the toggle only when the assumption differs |
| `bordbar.command.brightness` | `{"value": 1\|-1}` | one brightness step |
| `bordbar.command.color` | `{"value": 1\|-1}` | one colour step |
| `bordbar.command.mode` | `{"value": 1\|-1}` | one mode step |
| `bordbar.command.speed` | `{"value": 1\|-1}` | one speed step |
| `bordbar.command.reset` | `{}` | assume off, then restart |
| `bordbar.state` | `{"power": bool}` | assumed state, retained |
| `bordbar.availability` | `{"online": bool}` | retained, `false` via last will |
| `bordbar.command.selftest` | `{}` | published by the device itself, see below |

Command subjects are deliberately **not** covered by the JetStream `BORDBAR`
stream. Were they archived, a reconnecting MQTT session could be handed stale
commands and switch the light — or reboot the device — unprompted.

## Verifying the subscription

A broker can refuse a subscription — NATS does when the permissions do not cover
the wildcard's base subject, since `bordbar/command/#` also matches
`bordbar/command` itself. PubSubClient never surfaces the negative SUBACK, so
the device would report a healthy connection and drop every command in silence.

After subscribing, the firmware publishes `bordbar/command/selftest` and expects
it back through its own subscription. If nothing returns within five seconds the
subscription is dead, and it reconnects with the same backoff a failed connect
uses — retrying faster would only flap the last will and hammer the KNX status
address. The self-test is consumed internally and never reaches the command
dispatcher.

Log lines to look for: `command subscription verified`, or
`self-test did not return — subscription refused`.

## Assumed state, and the reset command

`power` is idempotent: it transmits only when the requested state differs from
what the firmware believes. That belief survives reboots (NVS) but cannot survive
someone picking up the physical remote — the firmware never hears it. After that,
belief and reality are inverted, and "on" switches the light off.

A raw toggle does not fix this: it flips reality *and* belief, so the offset
persists. The way out is to change one of the two alone, which is what `reset`
does. **Switch the light off with the remote, then send reset.** Both are off
afterwards. Sending reset while the light is on just re-inverts the pair.

The same command reboots a wedged device without opening the cabinet.

## Configuration

No credentials are compiled in, so the release `.bin` is safe to publish and
passwords rotate without reflashing.

On first boot the device opens the access point **`bordbar-setup`** with a
captive portal asking for the WiFi credentials plus MQTT host, port, user,
password and an OTA password. Everything is stored in NVS. An empty OTA password
leaves OTA disabled.

To change it later, press **`p`** on the serial console, or **hold BOOT (GPIO 0)
for three seconds while the device is running** — the portal reopens with the stored values prefilled and the device
restarts afterwards. Note that holding BOOT during reset does *not* work: it is a
strapping pin, so the chip would enter the ROM download loader and never reach
the application.

## Serial diagnostics

The serial console transmits codes directly, without network, MQTT or KNX:

| Key | Code |
|---|---|
| `p` | reopen the configuration portal |
| `1` | on/off toggle (also flips the assumed state) |
| `2` / `3` | brightness up / down |
| `4` / `5` | colour up / down |
| `6` / `7` | mode up / down |
| `8` / `9` | speed up / down |

On a device that never reports back this is the only way to tell a dead
transmitter from a dead bus, so it stays in the production build.

## Build and flash

```sh
pio run              # build
pio run -t upload    # flash over USB
pio device monitor   # serial log at 115200
```

Over the air, once the device is built into the cabinet — same binary, only a
different upload path, so this is a command-line override rather than a second
build environment:

```sh
PLATFORMIO_UPLOAD_PROTOCOL=espota \
PLATFORMIO_UPLOAD_PORT=bordbar.zimmermann.eu.com \
PLATFORMIO_UPLOAD_FLAGS=--auth=<ota-password> \
  pio run -t upload
```

Generate that password with `openssl rand -base64 32 | tr -dc 'A-Za-z0-9' | head -c 24`
— alphanumeric only, so it survives the shell without quoting — enter it in the
portal and keep it. Without it the only way back in is USB.

## License

GPL-2.0 — see [LICENSE](LICENSE).
