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

1. **Current vehicle profile.** There are no cyclic messages or physical signals.
   Start reports that a raw message must be added or the recording played.
   Click **Add raw test message**, then **Start**: `0x123`, DLC 1, data `05`
   is sent every 100 ms. Try `SET 0x123 0 127` or `INTERVAL 0x123 500`.
   This explicitly added message is synthetic, not a car protocol definition.
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
5. **Try an invalid value.** `SET 0x123 0 256` returns an error and leaves the
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
`python tools/check.py` runs all of these plus the converter/profile, native C++
and isolated HTTP checks in one command. `tools/test_demo_http.py --spawn` can
also run HTTP checks alone without a manually started server.

For UI acceptance, start the demo and run:

```text
npm install --prefix .tools/browser --no-audit --no-fund playwright@1.61.1
.tools/browser/node_modules/.bin/playwright install chromium
node tools/test_demo_browser.cjs
```

The browser test covers the real controls, raw byte edits, fault/recovery, AUTO,
replay completion, switching back to raw messages and mobile overflow. It saves
review screenshots under `build/`. You can pass a different server URL as its
first argument. Windows users can use the `.cmd` executable under `.bin`.

The raw-message button stops scheduling and selects MANUAL before adding 0x123.
This also works after a completed recording replay.

The PC demo does not verify ESP32 performance, real arbitration, the transceiver,
wiring or the car's message map. Use the
[hardware test guide](../docs/HARDWARE_TEST.md) for those.
