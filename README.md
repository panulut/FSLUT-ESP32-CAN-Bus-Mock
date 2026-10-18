# Formula Student CAN simulator

Firmware for bench testing with an ESP32 and an MCP2515 CAN controller. It
provides cyclic transmission, runtime editing, automatic signal ramps, timed
recording replay, CAN reception and USB serial control. A TJA1051 handles the
physical CAN interface. The LoRa example code is not used.

**The firmware has been built for the ESP32 and tested on a PC. Real hardware
wiring and bus operation have not been tested yet.** See the
[hardware test guide](docs/HARDWARE_TEST.md) and the
[validation status](docs/VALIDATION.md).

## Message definition status

The current source is [docs/CAN.xlsx](docs/CAN.xlsx), supplied on 2026-10-18.
The project manager explicitly deferred heartbeats `0x701`, `0x500` and `0x702`.
They are not included in the default profile, regardless of workbook fill colours.
The original ZIP and its extracted workbook are retained as historical inputs.

`config/vehicle.json` currently has no cyclic messages or physical signals.
`START` in MANUAL/AUTO reports an error until a raw message is added or a confirmed
profile is compiled. `PLAY` works immediately with the supplied motor recording.
The firmware boots stopped with the CAN controller in listen-only mode.

The updated workbook now names the motor fields, but first-phase CAN IDs and
several encoding details still need confirmation. `SET_SOC` and `SET_RPM` report
missing definitions rather than guessing the car's encoding. See the
[field map and open questions](docs/PROTOCOL.md),
[code walkthrough](docs/CODE_WALKTHROUGH.md) and
[requirement coverage](docs/REQUIREMENTS.md).

Raw bench messages can be added deliberately with `ADD`. Recording replay keeps
its original IDs, payloads and DLC, including `0x297` (not relabelled `0x294`).

## Wiring and configuration

`python tools/check.py` runs the native, demo engine and isolated HTTP tests.
The optional browser test and CI jobs are described in
[requirement coverage](docs/REQUIREMENTS.md).

Want to try it without hardware first? Run `python tools/demo.py` and
open the [local browser demo](http://127.0.0.1:8765).
The demo runs the same C++ core, recordings and commands on a software test bus.
[Demo guide and examples](demo/README.md).

Edit `include/board_config.h` to match your board and module. The current
**example settings** are a classic ESP32 Dev Module, a 16 MHz MCP2515 crystal,
1 Mbit/s and 1 MHz SPI:

| ESP32 | MCP2515 module |
|---|---|
| GPIO18 | SCK |
| GPIO19 | SO / MISO |
| GPIO23 | SI / MOSI |
| GPIO5 | CS |
| GND | GND |

No INT connection is needed: reception is polled over SPI.
GPIO5 is a strapping pin on the classic ESP32 that affects boot; check your board
wiring or choose another suitable CS pin.

Check the module's supply voltage and logic levels before wiring. ESP32 GPIOs
are not 5 V tolerant. Supply and VIO wiring differs between TJA1051 variants;
check the datasheet of your exact part. The MCP2515 TXCAN/RXCAN connect to the
transceiver and the transceiver's CANH/CANL to the test bus. Each physical end of
the bus needs a proper 120 ohm termination resistor, plus a common ground
reference. Use this only on a dedicated test bench.

## Building and flashing

The project uses PlatformIO. The ESP32 platform is pinned to version 6.10.0
(Arduino-ESP32 2.0.17). No external CAN libraries are needed; a minimal MCP2515
driver that is testable at register level is included.

With PlatformIO installed:

```powershell
python tools/generate_profile.py config/vehicle.json include/vehicle_profile.h
python tools/can_to_inc.py examples/motor_excerpt.txt include/recording.inc --loop-gap-ms 190
pio run
pio run --target upload --upload-port COM5
pio device monitor --port COM5 --baud 115200
```

Replace `COM5` with your device's port. In this workspace PlatformIO was installed
locally into the `.tools` folder. With it, the same commands are
`powershell -File tools/pio.ps1 run`,
`powershell -File tools/pio.ps1 run --target upload --upload-port COM5` and
`powershell -File tools/pio.ps1 device monitor --port COM5 --baud 115200`.
The firmware has not been flashed to a device as part of this implementation.

If PlatformIO is missing from another workspace, install PlatformIO IDE or install
`platformio==6.1.18` into a Python virtual environment. Then use that environment's
`python -m platformio` command instead of `pio`.

## First try

The serial port runs at 115200 baud. Commands are case sensitive.
Terminate each command with LF, CR or CRLF. Send one command at a time and wait
for the reply. Numbers are decimal or `0x`-prefixed hexadecimal: `50` means
decimal 50, `0x50` means 80. Physical values use a decimal point.

Without a second CAN node you can use the MCP2515's internal loopback mode:

```text
CONFIG 500000 16000000 LOOPBACK
MONITOR ON
ADD 0x123 1000 1 5
START
STATUS
STOP
```

`0x123` is an explicitly added synthetic bench message, not a vehicle definition.

Loopback checks the controller's internal transmit/receive path. It does not check
the transceiver, cabling or acknowledgement by another node.

On a normal test bus a second active CAN node is needed to acknowledge
transmissions. A listen-only adapter is not enough:

```text
CONFIG 1000000 16000000 NORMAL
MONITOR RX ON
MONITOR TX ON
ADD 0x123 1000 1 5
START
SET 0x123 0 127
INTERVAL 0x123 5000
STATUS
STOP
```

## Commands

| Command | Action |
|---|---|
| `HELP` | Command names |
| `START` / `STOP` | Start the current mode / stop and request abort of a pending transmission |
| `PAUSE` | Pause scheduling; a frame already queued may complete |
| `STATUS` | State, settings, queued/success/error counters and monitor drops |
| `LIST` | Messages and physical signal definitions; ends with `OK END LIST` |
| `MODE MANUAL` | Cyclic messages with user values |
| `MODE AUTO` | Cyclic messages plus linear ramps defined in the profile |
| `MODE PLAYBACK` | Recording replay |
| `SET 0x123 0 5` | Edit one raw message byte; byte index starts at zero |
| `SET_SIGNAL SIGNAL_NAME 127` | Physical value of a named signal |
| `SET_SOC 75.5` / `SET_RPM 3500` | Aliases for the profile's `SOC`/`RPM` signals; not present in the default profile |
| `FRAME 0x123 1 0x05` | Update a whole existing message: ID, DLC, bytes |
| `ADD 0x123 1000 1 5` | Example of a **user-defined raw message**: ID, interval ms, DLC, bytes; not a confirmed definition of the car's protocol |
| `ENABLE 0x123 OFF` / `ON` | Remove a message from / return it to cyclic transmission |
| `INTERVAL 0x123 1000` | Transmission interval 1..3600000 ms |
| `PLAY` | Select replay if needed and start/resume |
| `RESTART` | Rewind replay; first `PAUSE` or `STOP`, then `RESTART` and `PLAY` |
| `LOOP ON` / `OFF` | Replay looping |
| `MONITOR ON` / `OFF` | RX and TX output together |
| `MONITOR RX ON` / `MONITOR TX OFF` | Each direction separately |
| `FILTER 0x301` / `FILTER OFF` | ID filter for monitor output; the receive counter counts all frames |
| `CONFIG 500000 8000000 NORMAL` | Bit rate, MCP2515 crystal Hz, NORMAL/LOOPBACK; only when stopped |
| `RECOVER` | Reinitialise the CAN controller with current settings; only when stopped |

Mode changes and reinitialisation require stopping and completion of any pending
transmission. `STOP` cannot pull back a frame already on the bus. An aborted
replay frame is kept for the next `PLAY`. `RESTART` does not start transmitting by
itself. A recording that has played to the end once requires `RESTART` before a new
`PLAY`. `ENABLE` affects only cyclic messages, not the recording. Runtime changes
are not preserved across reboots.

Only `CONFIG`/`RECOVER` and CAN mode changes may wait on hardware, for a bounded
time. Active transmission, replay and monitoring use no blocking delay loops.
Invalid command lines are rejected as a whole before any message is updated.
Maximum length is 191 characters.

## Recordings

CSV format:

```csv
timestamp_ms,can_id,dlc,data
100,0x301,7,00 10 20 30 40 50 60
110,0x6B0,8,01 02 03 04 05 06 07 08
```

The optional `extended` and `rtr` columns accept `true`/`false` or `1`/`0`.
Without an `extended` column, an ID above 0x7FF is treated as extended. An extended
frame with a small ID requires `extended=true`. RTR frames have empty data, but the
DLC may be 0..8. Cyclic serial commands and signal profiles use 11-bit data frames;
recording replay and reception also support 29-bit and RTR frames. CAN FD is not
supported.

TXT format:

```text
0x0301 2144402 PDO { 0x0D 0x05 0x16 0x00 0x00 0x00 0x00 }
```

The converter validates IDs, DLC, every byte and timestamp order.
The first timestamp is shifted to zero; other relative times are preserved.
The order of frames with the same timestamp is preserved. The recording is not
reordered and seven-byte frames are not padded to eight bytes.

The included excerpt contains 49 consecutive raw frame lines from lines 126–174 of
the original recording. Its duration is 1805 ms; the example uses a 190 ms gap
between the last frame and the first frame of the next loop. This loop gap is a
test setting. The full original log contains untimed SDO lines, missing EMCY
payloads and adapter text notices; it must be trimmed to suitable recording
segments before lossless replay. `--skip-decoded-events` allows skipping EMCY text
lines **while reporting the number of skipped lines**. It does not fix SDO
timestamps. `--ignore-nonframes` allows skipping other textual metadata, but not
malformed `0x`-prefixed frame lines.

```powershell
python tools/can_to_inc.py my.csv include/recording.inc --loop-gap-ms 50
```

The default limit is 100000 frames (`--max-frames`). The recording is placed in
ESP32 flash as a `constexpr` array and is not copied to RAM. With this toolchain
the structure uses about 20 bytes per frame. The actual maximum size depends on
the firmware partition; the compiler checks the final size. The maximum relative
recording duration is uint32 milliseconds, about 49.7 days. After conversion the
firmware must be rebuilt and reflashed.

## Extending the signal profile

Edit `config/vehicle.json` and run `generate_profile.py`. Each message defines
ID, DLC, default bytes, interval, checksum and protocol source. Each signal has a
name, first byte, width of 1–4 bytes, byte order, signedness, scale, offset,
physical limits and default value.

The encoding is `raw = round((physical - offset) / scale)`. The nearest
representable value is sent; for example SOC's 0.5 % resolution requires a scale of
0.5, if confirmed by the protocol. Sub-byte bit fields are not supported yet: they
can be edited as raw bytes or added as a separately verified encoding. Overlapping
fields and unrepresentable limits are rejected. Names are unique and uppercase.

`rate_per_second` drives the linear change in AUTO mode. A positive value
increases and a negative value decreases the value per second; on reaching the
minimum or maximum the value stays at the limit. This allows defining an RPM
rise, SOC drop and current change as soon as their car-specific encoding is
confirmed. The default profile has no physical signals. The PC demo offers a
separately labelled synthetic RPM/SOC ramp test.

`checksum="sum_id_dlc"` is **only for a separately confirmed protocol**:
the eighth byte becomes `(CAN_ID + DLC + sum(bytes[0:7])) & 0xFF`.
The generator then requires a `checksum_source` field. The default profile does
not use the algorithm. The Excel description of the cell broadcast has not been
generalised to BMS messages.

The C++ API is `Simulator::add`, `setMessage`, `setByte`, `setSignal`,
`setBatterySOC`, `setRPM`, `setInterval`, `start`, `stop`, `setMode`, `load`,
`restart` and `tick`. `Transport::offer` separates frame building from hardware.
The hardware reports the result of a queued replay frame with `transmitResult`
before the next replay frame. The API is meant for a single main loop; it must not
be called concurrently from an interrupt or another FreeRTOS task.

## Timing, errors and capacity

- Scheduling uses the ESP32's 64-bit monotonic clock. `millis()` overflow
  does not interrupt long runs.
- Cyclic messages use phase-locked deadlines and fair round-robin. Late cyclic
  rounds are skipped; `cyclic_skipped` reports how many. Stale messages are not
  sent in a burst.
- Replay never drops frames when the buffer is full. Frames that are behind
  schedule are sent in order; `replay_late` counts lateness at queueing time.
  The schedule is a millisecond-level target. Bus arbitration and load can
  delay the actual transmission.
- A single CAN transmit buffer is used to preserve frame order.
  `TX_QUEUED` is the number of accepted transmit requests and `TX_OK` the number
  the controller reported as successful. In loopback mode TX_OK does not mean an
  external ACK.
- Bus-off or a transmission waiting more than 100 ms stops the run. Fix the cause
  and use `RECOVER`, then `PLAY`/`START`. A failed replay frame is kept for retry.
  The controller may retry the same frame until the timeout.
  If the controller does not respond to an abort either, at 200 ms the software
  transmit request is released as failed and the controller is marked
  uninitialised so that `RECOVER` can attempt a hardware reset.
- RX is checked on every main loop iteration, and both MCP2515 receive buffers
  are used. Overruns are counted. The maximum lossless load must be measured on
  real hardware; a nominal 1 Mbit/s does not mean a fully loaded bus can be
  printed to the serial port.
- Serial output uses a fixed 8192-byte queue, of which 1024 bytes are reserved
  for replies. Only as much as fits in the free buffer space is written to the
  UART. Monitor overload drops **output lines**, and `monitor_drop` shows the
  count. Any loss of replies is shown in the `reply_drop` counter. Settings
  and message storage are fixed size: 32 messages and 64 signals.
- Exact bit timing is computed for 8/16/20 MHz crystals. An impossible
  combination, such as 8 MHz and 1 Mbit/s with at least eight time quanta, is
  rejected. The sample point is shown in `STATUS`; timing for long cabling must
  be measured.

## Tests and structure

```powershell
python tools/check.py
```

Requires Python 3.10+ and a C++17 compiler. `--cxx PATH` selects a g++/clang++/Zig
compiler. A local Zig in this workspace is found automatically.
The Python tools have no external dependencies. The PC tests need no Arduino
libraries or device.

| Path | Contents |
|---|---|
| `src/main.cpp` | ESP32 main loop, SPI and serial port |
| `include/can_core.h` | Messages, signals, scheduling, automation and replay |
| `include/mcp2515.h` | Controller, bit timing and transmit/receive state |
| `include/commands.h` | Validated command parser |
| `include/output_queue.h` | Bounded, non-blocking output queue |
| `config/vehicle.json` | Editable source for message definitions |
| `include/vehicle_profile.h` | Generated profile |
| `include/recording.inc` | Generated recording |
| `examples/motor_excerpt.txt` | Excerpt from the original recording |
| `tools/` | Converters, test runner and local PlatformIO launcher |
| `tests/` | C++ and Python tests |

The MCP2515 driver is based on [Microchip datasheet DS20001801K](https://ww1.microchip.com/downloads/en/DeviceDoc/MCP2515-Family-Data-Sheet-DS20001801K.pdf),
in particular the descriptions of the transmit, receive, bit timing and SPI
registers. Other technical sources:
[ESP32 SPI](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/spi.html),
[ESP32 UART](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/serial.html) and
[PlatformIO esp32dev](https://docs.platformio.org/en/stable/boards/espressif32/esp32dev.html).
