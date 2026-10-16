# Browser demo without an ESP32

Start from the project root:

```powershell
python tools/demo.py
```

The command builds the PC demo engine if needed, starts a server that listens only
on this machine at `http://127.0.0.1:8765` and opens the browser.
You need a C++17 compiler; a local Zig in this workspace is found automatically.
On later runs the existing `build/demo_engine.exe` is reused if its sources have
not changed. If the port is taken, use for example
`python tools/demo.py --port 8766`. Stop the server with Ctrl+C in the terminal.

The demo uses the `include/can_core.h` and `include/commands.h` classes directly.
It does not reimplement their logic in JavaScript. Python forwards commands to
the C++ process, and the browser shows the real states and frames it produces.
The demo does not use SPI or physical MCP2515 hardware. A software test adapter
acknowledges each frame after 1 ms; a missing external ACK can be simulated with
the 100 ms timeout. The RX button injects a software receive event. Monitoring in
both directions is enabled by default in the demo.

## Try this

1. **Current vehicle profile → Start.** The monitor shows `0x701`, DLC 1, data
   `05` once per second. Change the state to `7F · Pre-operational` and the
   interval to `100 ms`: the next frames show the change and faster transmission.
2. **Replay from start.** You see the 49 frames of the original recording. The DLC
   of `0x301` is 7. Pause, resume and try looping. Replay uses the same generated
   `.inc` recording as the ESP32 firmware.
3. **Automatic ramp test → Start.** RPM rises by 600 units per second and
   SOC falls by 0.5 percentage points per second. The values are encoded into the
   `0x100` test message. This profile is intentionally synthetic and does not
   change the vehicle JSON profile or the firmware. You can also try
   `SET_RPM 3500` and `SET_SOC 75.5`.
4. **Disable the ACK** while transmitting. Transmission stops, the error counter
   increases and an error message appears. Re-enable ACK, press `RECOVER` and
   start again.
5. **Try an invalid value.** `SET 0x701 0 256` returns an error and leaves the
   message unchanged. The command line uses the firmware's real parser.

In the current vehicle profile `SET_SOC`/`SET_RPM` intentionally still return an
error: the car's corresponding signal definitions are missing. Switching profiles
resets the demo counters. The monitor shows the last 400 events, and the page's
clear button does not reset the simulator itself. Starting the server or switching
profiles begins a new demo state. Multiple browser tabs control the same
simulation session.

## Tests and scope

```powershell
python tools/demo.py --self-test
```

The integration test checks the stopped initial state, cyclic transmission,
signal editing, an invalid value, replaying the recording to the end and its
DLC, automatic ramps, ACK timeout, recovery and an RX event.
The original PC tests are still run with `python tools/check.py`.

The PC demo does not verify ESP32 performance, real arbitration, the transceiver,
wiring or the car's message map. Use the
[hardware test guide](../docs/HARDWARE_TEST.md) for those.
