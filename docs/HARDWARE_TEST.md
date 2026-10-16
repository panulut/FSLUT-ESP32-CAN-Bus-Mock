# Hardware acceptance testing

These tests are meant to be run on a real ESP32 + MCP2515 + TJA1051 setup.
They have not been run yet. Record the board, module crystal, exact transceiver
variant, pins, software version, adapter and timestamps of observations. Use a
dedicated test bench.

## 1. Electrical check and power-up

1. Check voltages and logic levels against each part's datasheet. No 5 V may
   reach an ESP32 input. Check how the CS pin affects booting.
2. Check the common ground reference and CANH/CANL. With the bus unpowered, the
   combined termination resistance is typically about 60 ohms when two
   120 ohm resistors are fitted at the correct ends.
3. Set the SPI pins and oscillator frequency in `board_config.h`.
   Build and flash the `esp32dev` firmware.
4. Open the serial port at 115200 baud. Send `STATUS`.
   Expected: `running=0`, `ready=1`, `fault=0`, `pending=0`.
5. Check with an external adapter that the software sends no CAN frames of its
   own at startup. Listen-only mode also does not acknowledge other frames.

## 2. Controller internal loopback

```text
CONFIG 500000 16000000 LOOPBACK
MONITOR ON
START
```

Change the oscillator value to match the actual module. Expected: about once
per second `0x701`, DLC 1, data `05`; RX and TX counters increase. Loopback tests
the internal controller path, not the transceiver or bus ACK.

```text
SET_SIGNAL NMT_STATE 127
INTERVAL 0x701 100
STATUS
STOP
```

Expected: data changes to `7F`, interval about 100 ms. After STOP, the cyclic
transmission ends; a frame already on the bus may still complete. Wait for
`pending=0` before the next initialisation.

## 3. Normal bus and reception

1. Connect a second active CAN node or an adapter in transmit/ACK mode.
   A listen-only adapter is not enough to acknowledge frames.
2. `CONFIG 1000000 16000000 NORMAL`, `MONITOR ON`, `START`.
3. Record ID, DLC, bytes and timestamps with the external adapter. Expected:
   `0x701` with the currently set value; on an error-free bus `TX_OK` increases
   and `TX_FAILED` does not. After STOP, once the queue has drained,
   `TX_QUEUED = TX_OK + TX_FAILED`.
4. Send an 11-bit 0x301 / DLC 7 with known bytes from the adapter.
   Expected: exactly 7 bytes in the RX output, no added eighth byte.
5. Also try a 29-bit frame and an RTR frame. Expected: correct ID,
   EXT/STD and RTR marking; no invented payload is shown for RTR.
6. `FILTER 0x301`: only its monitor lines are shown, but the RX counter still
   counts all received traffic. `FILTER OFF` restores output.

## 4. Recording order, pausing and timing

```text
STOP
MODE PLAYBACK
RESTART
LOOP OFF
MONITOR TX ON
PLAY
```

Wait until the last transmission has completed. The external capture must
contain 49 data frames in the same order and with the same DLC and payload as
`examples/motor_excerpt.txt`. The target duration from first to last is
1805 ms; measure the deviation and report the measurement method. USB/SPI polling,
arbitration and the other node may add latency. Check that no 0x301 frame grows
from seven bytes to eight.

- `LOOP ON`, `RESTART`, `PLAY`: check the gap between loops. In the supplied
  example the target from last to next first frame is 190 ms.
- `PAUSE`: scheduling stops, but a frame already queued may complete.
  `PLAY` resumes from the next frame, keeping the remaining wait time.
- `STOP` while a frame is queued: if the abort succeeds, the frame is kept for
  retry. A frame already acknowledged as successful is not retried.
- `PAUSE`, wait for `pending=0`, `RESTART`, `PLAY`: replay starts from the beginning.
- Load the bus with another node. Check that order is preserved,
  `replay_late` reports lateness and congestion does not drop replay frames.
  An ACK error or the 100 ms timeout stops replay; that is a separate fault test.

## 5. Serial errors and load

1. Send `SET 0x701 0 256`, `INTERVAL 0x701 0`, `SET_SOC 75.5`,
   `FRAME 0x701 1 0xGG` and a line longer than 191 characters. Expected: `ERR` and
   previous data is unchanged. `SET_SOC` fails in the default profile because the
   definition is missing.
2. Try `MODE AUTO` while transmitting. Expected: `ERR`; after stopping, the
   mode change succeeds.
3. Try `CONFIG 1000000 8000000 NORMAL`. Expected: `ERR`, because the timing
   cannot be built with the allowed time quanta. The old configuration is kept.
4. Send with LF, CR and CRLF line endings. Each command must execute once.
5. Load RX and keep monitoring enabled. Measure transmission timing.
   A slow serial port may drop monitor lines (`monitor_drop` increases),
   but the main loop must not block on output. Also watch the `RX_OVERRUN`
   counter; the limit caused by the MCP2515's two RX buffers must be measured.
6. Compare load with monitoring off/on and record the highest lossless
   receive rate for this setup. Check that commands still get a reply.

## 6. Missing ACK, bus-off and recovery

1. Disconnect the other node or switch it to listen-only and start normal
   transmission. Expected: after at most 100 ms of TX wait, `fault=1`,
   `running=0`, failure counter increases. Bus-off may trigger before that;
   ACK errors may also stay in error-passive state before the timeout.
2. Check `TEC`, `REC`, `EFLG` and external bus monitoring.
3. Restore the correct bit rate and an acknowledging node. Wait for `pending=0`,
   use `RECOVER` and then `START` or `PLAY`. RECOVER does not start transmitting by itself.
4. Verify in replay that the failed/aborted frame is retried from the same
   position. A reboot instead resets settings and replay to the beginning.

## 7. Long run and acceptance record

Run for several hours with a representative traffic load. Record bus load,
cyclic transmission timing deviations, replay lateness, RX overruns and UART
output drops. If you add physical BMS/motor signals, compare their raw encoding
with an independent manufacturer specification using known negative, positive
and boundary values. Only after successful hardware and protocol tests can the
setup be considered accepted for that test bench.
