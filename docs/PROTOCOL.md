# Protocol scope and open questions

## Source precedence

1. The project manager's feedback explicitly defers `0x701`, `0x500`, `0x702`
   heartbeats. The task also defers fault messages and error codes.
2. [CAN.xlsx](CAN.xlsx) is the workbook supplied on 2026-10-18, copied unchanged.
3. `CAN_SIM.md` is the original task. `CAN SIM.zip` and its extracted files are
   historical inputs, including the old workbook and the recorded motor traffic.

The new workbook's heartbeat ID cells B7, B17 and B18 still use green accent6
(`70AD47`, lightened), despite the feedback referring to yellow. Colours alone
therefore cannot identify the first-phase messages. No cyclic vehicle messages
are enabled in `config/vehicle.json` until selection and encoding are confirmed.
This is an incomplete vehicle profile, not completion of the physical-signal task.
Raw `ADD`, recording `PLAY`, RX monitoring and synthetic PC ramp tests remain usable.

## Field map from the updated workbook

Source: `CAN.xlsx`, sheet `Name`, B1:J19. Columns C:J are bytes 0:7.
The positions below are **starting byte indexes**, not inferred field widths.
Merged cells indicate some spans, but single unmerged labels do not establish a
field's width. None of these rows is automatically converted into a physical
signal definition. IDs are hexadecimal.

| ID | Row | Field starts (byte: label) | Status / missing details |
|---|---|---|---|
| 0x154 | 2 | 0: Target_Id_If; 2: Target_Iq_Ia; 4: Id_If; 6: Iq_Ia | Four two-byte spans; order, signedness, units/scales and interval unconfirmed |
| 0x128 | 3 | 0: Ud; 2: Uq; 4: Voltage modulation %; 6: Measured inductance | Four two-byte spans; order, signedness, scales and interval unconfirmed |
| 0x294 | 4 | 0: Voltage limit circle magnitude; 2: Maximum fluxing current; 4: Maximum iq allowed; 6: Motor Temperature 1 | Recording contains 0x297 instead; do not substitute IDs or decode 0x297 with this row |
| 0x301 | 5 | 0: Capacitor Voltage; 2: Battery Voltage; 4: Battery Current; 6: Target torque | Byte 7 has a red missing-data/padding note; recorded DLC is 7. No automatic padding or invented torque byte |
| 0x298 | 6 | 0: Maximum motor speed; 4: Velocity | Widths, order, signedness, units/scales unconfirmed; do not assume velocity is RPM |
| 0x701 | 7 | 0: NMT state | Deferred heartbeat. Recording confirms one-byte 05/7F; not enabled cyclically |
| 0x6B0 | 8 | 0: Pack Current (0.1 A); 2: Pack Inst. Voltage (0.1 V); 4: SOC (0.5%); 5: Relay State; 7: CHECKSUM | Order, signedness, relay bits, checksum and interval unconfirmed |
| 0x6B1 | 9 | 0: DCL; 2: CCL; 7: CHECKSUM | Units explicitly ambiguous: 0.1 kW or 1 A. Do not choose one from the Explanation sheet alone |
| 0x6B2 | 10 | 0: High Cell Voltage (0.0001 V); 2: Low Cell Voltage (0.0001 V); 4: High ID; 5: Low ID; 7: CHECKSUM | Order, ID numbering, checksum and interval unconfirmed |
| 0x6B3 | 11 | 0: Highest Temperature (1 C); 1: Lowest Temperature; 2: Average Temperature (1 C); 3: High Temp ID; 7: CHECKSUM | Signedness/offset, ID numbering, checksum and interval unconfirmed |
| 0x6B4 | 12 | All bytes labelled blank | No useful payload definition |
| 0x036 | 13 | 0: Cell ID; 1: Instant Voltage (16 bit, 0.1 mV); 3: Internal Resistance (15 bit, 0.01 mOhm); 5: Open Voltage (16 bit, 0.1 mV); 7: CHECKSUM | Order, resistance bit placement and ID numbering unconfirmed; checksum recipe exists specifically for this broadcast |
| 0x076 | 14 | Thermistor broadcast; no field labels | Payload missing |
| 0x600 | 15 | 0: 5; 1: DTC Status #1; 3: DTC Status #2; 5: Failsafe Statuses; 7: CHECKSUM | Fault/heartbeat message deferred by task |
| 0x601 | 16 | 0: 5; 1: DTC Status #1; 3: DTC Status #2; 5: Failsafe Statuses; 7: CHECKSUM | Charging fault/heartbeat message deferred by task |
| 0x500 | 17 | 0: Insulator heartbeat state; 1: SEVCON CAN error; 2: CAR CAN error | Deferred by explicit feedback; DLC and error encoding unconfirmed |
| 0x702 | 18 | 0: 05 operational / 7F error; 1: Isolator Error Code | New ID compared with old workbook; deferred by explicit feedback |
| 0x007 | 19 | Which TPD was updated in packet mode; no payload map | Payload missing |

Blank bytes are not silently assigned values or treated as verified reserved bytes.
The task's example physical values and API names are illustrative, not wire-format
reference vectors.

## Checksums

`Name!C29:C32` describes the **cell broadcast** checksum: ID + length 8 + bytes
0–6, truncated to an unsigned byte. The wording about chopping off bits is
ambiguous, but the unsigned-byte description corresponds to retaining the low
8 bits. Obtain a known-good reference frame before using it for a vehicle message.
Do not generalise this recipe to every CHECKSUM label in the workbook.

The generic `sum_id_dlc` algorithm remains tested with a synthetic vector. The
profile generator requires `checksum_source` before allowing that algorithm.
This records the user's verification source; it cannot independently validate it.

## Questions to resolve with the project manager

1. Which non-heartbeat CAN IDs belong in the first phase? Use an explicit list
   because the workbook colours and the feedback disagree.
2. Is Excel 0x294 correct, is recorded 0x297 correct, or are both separate messages?
3. For each selected ID: confirm DLC, field widths/bit positions, byte order,
   signedness, scale/offset, valid range, defaults, interval and checksum/counter.
   A DBC or manufacturer message map with known raw/physical pairs is suitable.
4. For 0x301: preserve DLC 7 or use a specified padded format? What is the target
   torque width/scale, and what exactly is missing in the SEVCON configuration?
5. If selected, confirm DCL/CCL units and the separate BMS checksum algorithms.

Add only confirmed definitions to `config/vehicle.json`, regenerate the header,
and test known physical/raw pairs against an independent reference decoder.
Signals named `SOC` and `RPM` enable their serial aliases automatically.

## Recording integrity

The original log contains SDO transmissions with a zero timestamp in between
other timestamps, and EMCY lines decoded to text without a raw payload. A complete
timed replay of the full log is not possible from this data without guessing.
The converter rejects these by default. The supplied `examples/motor_excerpt.txt`
is an unmodified excerpt of original lines 126–174, apart from a comment header.
A test compares the excerpt with the original source and the generated data with
the converter output.

The `.inc` example in the LoRa folder is not used as a replay source: it lacks the
original timestamps and DLC information. The seven-byte `0x301` frame remains a
seven-byte frame in the new converter.
