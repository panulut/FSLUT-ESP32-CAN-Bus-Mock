# Code walkthrough and fault tracing

## What happens, when and why

| Stage | Code | Behaviour and reason |
|---|---|---|
| Build | `config/vehicle.json` → `tools/generate_profile.py` → `include/vehicle_profile.h` | Only explicit confirmed signal definitions become C++ code. The profile is currently empty because heartbeats are deferred and other encodings await confirmation. |
| Boot | `src/main.cpp`, `App`, `loadVehicleProfile` | Load the profile and compiled recording. Configure MCP2515 in listen-only mode. Transmission requires an explicit command. |
| Command | `include/commands.h`, `Commands::feed/execute` | Parse one serial line, validate values, then change the simulator or call the host adapter. An empty MANUAL/AUTO profile rejects START with instructions. |
| Raw cyclic setup | `ADD 0x123 100 1 5` | Add a synthetic one-byte frame to RAM at a 100 ms interval. It is not persisted across reboot. SET changes bytes; FRAME replaces a payload with the same DLC. |
| Start | `App::start`, `Simulator::start` | Activate the controller and initialise scheduling. PLAY selects the recording instead of cyclic messages, so it works without a vehicle profile. |
| Each loop | `Simulator::tick` | MANUAL sends due messages. AUTO updates defined signal ramps then schedules. PLAYBACK offers one recorded frame when its relative timestamp is due. No blocking playback delay is used. |
| Queue | `Transport::offer`, `Mcp2515::offer` | A successful offer means queued, not acknowledged. Keep this distinction when interpreting counters. |
| Completion | `Mcp2515::service`, `Simulator::transmitResult` | Hardware completion reports success/failure. Failed replay frames remain available for explicit retry. A missing ACK timeout stops transmission. |
| Observe | `OutputQueue`, MONITOR, FILTER, STATUS | Serial output is buffered so a slow terminal does not block CAN processing. Monitor output can be dropped; counters expose this. |

## Try a raw message without inventing a vehicle signal

On the PC demo select Current vehicle profile, then enter:

```text
ADD 0x123 100 1 5
START
SET 0x123 0 127
INTERVAL 0x123 500
STOP
```

Expect hex data 05, then 7F. The interval changes from 100 to 500 ms.
These are raw bytes: 127 is not a temperature, RPM or state of the actual car.
On hardware first configure the actual crystal/bitrate and use loopback or a
properly wired bench with an acknowledging node. See HARDWARE_TEST.md.

For recorded motor data, use PLAY. The 49-frame excerpt preserves the original
IDs and DLC, including seven-byte 0x301 frames and 0x297. The new workbook's
0x294 field names do not justify changing the recording's 0x297 ID.

## Diagnose a problem

| Symptom | First checks | Relevant code |
|---|---|---|
| START reports no cyclic messages | Expected for the empty vehicle profile. Add an explicit raw message or PLAY. | `Commands::execute`, `Simulator::start` |
| SET_SOC / SET_RPM reports missing definition | Confirm the car's encoding, then add SOC/RPM to JSON and regenerate/rebuild. The demo's synthetic ramp is separate. | `generate_profile.py`, `Simulator::setSignal` |
| A physical value produces wrong bytes | Check definition byte index, width, endian, signedness, scale and offset against a known frame. Do not fix it by changing the ID or padding blindly. | `Simulator::encode/decode`, `docs/PROTOCOL.md` |
| Frames queued but no successful TX | Inspect bitrate/crystal, wiring, second node ACK and STATUS error counters. Re-enable ACK and RECOVER after resolving the fault. | `Mcp2515::service`, `App` |
| Playback resumes at an unexpected frame | Inspect position, pending frame and STOP versus PAUSE semantics; a failed frame is retained for retry. | `Simulator::transmitResult`, `restart` |
| Monitor lines disappear | Inspect monitor drops, UART capacity and RX overruns. A displayed line count is not a bus delivery guarantee. | `OutputQueue`, `Mcp2515::receive` |

## Verify an edit

Run `python tools/check.py` for conversion/profile, native C++, shared demo
engine and isolated HTTP checks together. `python tools/demo.py --self-test`
and `python tools/test_demo_http.py --spawn` can run those parts separately.
See REQUIREMENTS.md for the optional browser acceptance test and CI jobs.
Build with PlatformIO and perform the hardware acceptance tests separately.
PC checks cannot prove electrical behaviour or validate undocumented vehicle data.
