# Validation status 2026-10-16

## Checks performed

| Check | Result |
|---|---|
| `python tools/check.py` | Passed: 10 Python tests and 725 C++ checks |
| `powershell -NoProfile -File tools/pio.ps1 run` | Passed: ESP32 firmware compiles and links |
| Profile regeneration | Generated header matches the JSON source |
| Recording regeneration | Generated `.inc` matches the source excerpt and the 190 ms loop gap |
| Source excerpt comparison | 49 frames match lines 126–174 of the original log |
| Original files | All 5 extracted source files match the original `CAN SIM.zip` archive byte for byte |

The Python tests cover CSV/TXT parsing, DLC/payload consistency, ID limits,
extended/RTR frames, timestamp ordering and duration, handling of missing data,
the effect of a failed conversion on an existing output file, and validation and
reproducible generation of signal profiles.

The C++ tests cover known signed and unsigned encoding vectors in both byte
orders, physical limits, 32-bit fields, a synthetic checksum, cyclic scheduling,
fair transmission, lateness, continuous running past the uint32 millisecond
limit, fractional accumulation in automatic ramps, replay timing and order,
pausing, looping, and retention of failed/aborted replay frames.

The MCP2515 tests use an injected register model: listen-only/normal/loopback
mode changes, exact bit rates with 8/16/20 MHz crystals, rejection of invalid
combinations, distinguishing queued from successful transmission, timeout,
bus-off, abort, an abort racing an already completed transmission, recovery of a
stuck controller, standard/extended/RTR reception, invalid DLC and receive buffer
overflows are all tested without hardware. Command and output queue tests cover
invalid values, argument counts, over-long lines, CRLF, signal alias errors, mode
changes, a stalled UART writer, queue wrap-around and capacity limits.

The register model verifies software behaviour. It does not emulate the CAN
electrical layer, every controller race condition or real arbitration.

## Build environment and artifact

- PlatformIO Core 6.1.18, `platformio/espressif32@6.10.0`, board `esp32dev`.
- Arduino-ESP32 package `3.20017.241212+sha.dcc1105b` (core 2.0.17).
- ESP32 compiler `8.4.0+2021r2-patch5`, C++17.
- PC tests: Python 3.13 and the Zig 0.13.0 C++17 compiler; the project's
  C++ tests were built with `-Wall -Wextra -Werror`.
- Firmware uses 36128 bytes of static RAM / 327680 bytes (11.0 %).
- Linked program uses 297969 bytes / 1310720 bytes (22.7 %).
- Flash image: `.pio/build/esp32dev/firmware.bin`, 298336 bytes.
- Flash image SHA-256:
  `98baab458101909c1e863e8167487a6ff39686274a0c1beb8c3cc193c03cd4f2`.

The hash applies to this build only. A new build may contain different metadata
and produce a different hash. Flash the device with the PlatformIO upload
command, which also writes the correct bootloader and partition table.

## Still to be verified

1. The actual ESP32 board pinout, the MCP2515 module crystal and the transceiver
   wiring are unknown. `board_config.h` contains documented examples.
2. The software has not been flashed or run on a real ESP32/CAN setup.
   External ACKs, physical reception, real latencies, maximum lossless load and
   long runs must be checked using the [hardware test guide](HARDWARE_TEST.md).
3. The physical definitions of the car's BMS and motor messages are incomplete.
   The default profile contains only 0x701/NMT_STATE. The RPM/SOC ramp engine and
   encoding are tested with synthetic signals, but the car's corresponding
   messages are not enabled without confirmed definitions.
   See [open protocol questions](PROTOCOL.md).

The result is a compiling software implementation verified by PC tests. It has
not yet been approved as a substitute for the real car's CAN protocol or a
physical test bench.
