# Validation status 2026-10-18

## Checks performed

| Check | Result |
|---|---|
| `python tools/check.py` | Passed: 10 Python tests, 733 native C++ checks, 12 demo integration checks, isolated HTTP acceptance test |
| AddressSanitizer and UndefinedBehaviorSanitizer | Passed: 733 C++ checks with `-fsanitize=address,undefined`; leak detection disabled because this host cannot support LeakSanitizer process inspection |
| `node tools/test_demo_browser.cjs URL` | Passed: empty-profile guidance, raw ADD/SET/TX, ACK fault/recovery, synthetic AUTO, completed replay, return to raw MANUAL, mobile overflow and no browser JS errors |
| Visual review | Desktop 1440 px and phone 390 px screenshots inspected during initial validation; grid overflow fixed and mobile payloads wrap |
| JavaScript syntax | `node --check demo/app.js` and browser test script passed |
| ESP32 firmware | Compiled and linked successfully during initial validation; no compiler warnings in the build log |
| Generated profile and recording | Reproducibility checked by Python tests; recording matches original excerpt lines and preserves DLC 7 for 0x301 |
| Updated workbook | `docs/CAN.xlsx` is an unchanged copy of the supplied newer Excel |
| Workflow configuration | YAML parsed and jobs inspected; remote GitHub Actions results must be checked separately |
| Diff | `git diff --check` passed |

`tools/check.py` runs the native, demo and HTTP checks in one command. The HTTP
test starts an isolated server on an ephemeral port and waits for conditions
with a deadline instead of depending on fixed sleeps. Browser testing is optional
locally and is included in the new CI workflow.

## Recovery before publication

Workspace maintenance removed the unpublished local checkout and build artifacts.
The changes were restored from the recorded patches onto original commit
`8ad9155`, and the newer workbook was recovered from the original attachment.
The firmware-facing patches (`can_core.h`, `commands.h`, generated profile) match
the previously recorded source changes. Python, native C++, demo, HTTP, browser,
and sanitizer checks were repeated on the restored checkout before publication.
The earlier ESP32 image and local build log were not recovered; the initial
build evidence below is historical. The added firmware CI job provides a fresh
build of the published commit; consult its actual result before claiming it passed.

## Initial ESP32 build evidence

Command used (telemetry disabled):

```sh
PYTHONPATH=.tools/python PLATFORMIO_CORE_DIR=.tools/platformio PLATFORMIO_SETTING_ENABLE_TELEMETRY=no python -m platformio run
```

For an ordinary PlatformIO installation use `python -m platformio run`.

- PlatformIO Core 6.1.18, `platformio/espressif32@6.10.0`, board `esp32dev`.
- Arduino-ESP32 package `3.20017.241212+sha.dcc1105b` (core 2.0.17).
- ESP32 toolchain `8.4.0+2021r2-patch5`, C++17.
- Static RAM: 36128 / 327680 bytes (11.0%).
- Application flash usage: 297205 / 1310720 bytes (22.7%).
- Flash image: `.pio/build/esp32dev/firmware.bin`, 297568 bytes.
- SHA-256: `d3e99a60a27b2974a2358afae0e20eee37cf4c9de8ad9a9cce2029253ab6e38d`.

This hash describes that initial build, not every future rebuild. Upload with
PlatformIO so the bootloader and partition table are flashed with the image.
Telemetry is disabled in CI.

The local browser tests used Playwright Core 1.61.1 and portable Chromium
143.0.7499.0. CI installs the standard Playwright 1.61.1 browser distribution.
Screenshots are local `build/demo-desktop.png` and `build/demo-mobile.png`.

## Remaining acceptance work

1. Confirm first-phase vehicle IDs and missing wire definitions listed in
   [PROTOCOL.md](PROTOCOL.md). The default profile contains no cyclic vehicle
   messages or physical signals. This is incomplete vehicle coverage, not a
   claim that an empty profile satisfies the assignment.
2. Verify actual ESP32 pins, MCP2515 crystal, transceiver variant, logic levels
   and wiring. Configuration still contains documented board examples.
3. Flash real hardware and run [HARDWARE_TEST.md](HARDWARE_TEST.md): external ACK,
   simultaneous RX/TX, timing, load, overruns, fault recovery and long runs.
4. Once physical signal definitions are supplied, check known raw/physical pairs
   with an independent reference decoder before treating the bench as the car.

The MCP2515 register model checks software behaviour. It does not emulate the
CAN electrical layer or prove physical arbitration, latency or lossless capacity.
See [requirement coverage](REQUIREMENTS.md) for the assignment mapping.
