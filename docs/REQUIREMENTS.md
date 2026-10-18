# Assignment coverage

This maps the supplied `CAN_SIM.md` to the implementation. It separates software
capabilities from the vehicle definitions and physical acceptance that remain open.

| Requirement | Evidence / entry point | Status |
|---|---|---|
| Initial selected vehicle messages | `config/vehicle.json`, `docs/PROTOCOL.md`, updated `docs/CAN.xlsx` | Blocked on first-phase IDs and wire definitions. No heartbeat exception. |
| Defaults, intervals, individual fields and whole payload edits | `Simulator::add/addSignal/setInterval/setByte/setSignal/setMessage`; serial ADD, SET, SET_SIGNAL, FRAME, INTERVAL, ENABLE | Implemented and tested with explicit synthetic messages. Car-specific physical defaults not claimed. |
| Correct ID, DLC, encoding and scale | Strict converter; generated profile validation; signed/unsigned C++ reference vectors in both byte orders | Generic capability tested. Vehicle encoding still requires confirmed reference frames. |
| Continuous cyclic transmission | `Simulator::tick`, round-robin scheduling, late-period skipping | Native tests cover scheduling, fairness and automatic ramps. Physical timing not measured. |
| CSV/TXT conversion and compiled playback | `tools/can_to_inc.py`, `include/recording.inc`, `examples/motor_excerpt.txt` | 49-frame source excerpt is reproduced exactly. Full source log has ambiguous zero timestamps and decoded events without raw payloads, so is not silently replayed as if complete. |
| Playback order, relative timing, pause/restart and looping | `Simulator::load/start/stop/restart/transmitResult/tick` | Native and demo tests. Original seven-byte 0x301 frames remain seven bytes. |
| Nonblocking playback and large recordings | Flash-resident constexpr recording; one offer per tick; bounded serial work | Architecture implemented. Actual maximum recording size and lossless load depend on ESP32 resources and must be measured for the dataset/hardware. |
| Automatic simulation | MANUAL/AUTO/PLAYBACK modes; per-signal clamped linear ramps | Generic RPM rise and SOC discharge tested in the separately labelled synthetic demo. No vehicle driving/charging/fault scenario model is claimed. |
| USB serial commands and meaningful errors | `include/commands.h`, bounded parser, `OutputQueue`; START rejects an empty cyclic profile with next steps | Native parser/queue tests. Physical USB behaviour awaits hardware. |
| TX and RX together | Main loop services TX completion and polls RX independently; `Mcp2515` adapter | Register-model tests cover completion, formats and overruns. Physical simultaneous traffic untested. |
| Monitoring, timestamps, filters and separate RX/TX controls | MONITOR, FILTER, STATUS, queued/success/failed counters | Implemented; slow serial output can drop monitor lines without blocking scheduling. Hardware limits unmeasured. |
| Firmware, converter, definitions and documentation | `src`, `include`, `tools`, `config`, README, protocol map and walkthrough | Delivered source. Vehicle definitions remain incomplete and are labelled accordingly. |
| Heartbeats/fault codes later; verified checksums only | Empty default profile; checksum requires explicit source | Applied. Raw commands/replay are not a hard ban on IDs: users may deliberately define a raw bench frame and replay preserves source traffic. |

## Repeatable verification

`python tools/check.py` runs converter/profile tests, builds and runs C++ tests,
then checks the shared demo engine and an isolated HTTP server. No manual server
startup is required. Use `--cxx PATH` to select a C++17 compiler.

The optional `node tools/test_demo_browser.cjs [URL]` drives the actual UI through
empty-profile guidance, raw editing/transmission, ACK fault/recovery, synthetic
AUTO, completed replay, switching back to raw messages, and a phone-width layout.
Install Playwright under `.tools/browser` and start `python tools/demo.py` first.

`.github/workflows/check.yml` defines native, sanitizer, browser and ESP32 build
jobs for future pushes/PRs. The presence of a workflow is not proof of a remote
CI run. Consult VALIDATION.md for checks actually run on the current revision.
