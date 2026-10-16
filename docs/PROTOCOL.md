# Protocol scope and open questions

The sources are the original `CAN_SIM.md`, `CAN SIM/CAN SIM/CAN.xlsx` and
`Some CAN DATA FROM MOTOR to use as mock data.txt` in the same folder. The original
files have not been modified. The Excel cell fill colours were read directly from
the file's style and theme definitions: green is accent6 `70AD47`, lightened.

| Messages | Excel `Name` sheet | Implementation decision |
|---|---|---|
| 0x701 | A7:J7, green, NMT state; 05 operational / 7F pre-operational | In profile: DLC 1, NMT_STATE with unmodified raw bits. DLC is confirmed by the recording. The 1000 ms interval is a bench default. |
| 0x500 | A18:I18, green; insulator heartbeat and two CAN error states | No default message. DLC, reserved bytes, error state values and interval must be confirmed. Can be defined by the user as a raw message. |
| 0x154, 0x128, 0x294, 0x301, 0x298 | A2:I6, mostly yellow; content empty | Raw/recording replay only. The recording contains 0x297 whereas the sheet lists 0x294: they are not treated as the same. |
| 0x6B0 | A8:I8, blue | Current 0.1 A, voltage 0.1 V and SOC 0.5 % are given in text. Byte order, signedness, relay field width and checksum are missing. No physical profile. |
| 0x6B1 | A9:I9, blue, red note in the DCL/CCL area | Unit is ambiguous in the source itself: 0.1 kW or 1 A. No physical profile. |
| 0x6B2 | A10:I10, blue | Cell voltage scale 0.0001 V and ID bytes are given, but byte order, checksum and ID numbering are missing. |
| 0x6B3 | A11:I11, blue | Temperature scale of 1 C is given. Signedness/offset and checksum are missing. |
| 0x6B4 | A12:I12, grey; blue on the Explanation sheet | Byte contents missing. No default message. |
| 0x036, 0x076 | A13:I14, light blue; checksum cell of 0x036 green | Cell and thermistor broadcasts. A single green checksum cell does not make the whole message a green-flagged message in the sense of the requirement. |
| 0x600, 0x601 | A15:I16, blue | Heartbeat/fault messages have been deferred. |
| 0x007 | A17:I17, orange | No default message. |

## Checksums

`Name!B30:B33` describes the sum for the **cell broadcast**: ID + length 8 + bytes
0–6, truncated to one byte. This has not been reliably generalised to every
CHECKSUM field in the sheet. The BMS manufacturer, model, configuration or official
message map were not provided. The default profile therefore does not generate
checksummed BMS messages.

The generic `sum_id_dlc` algorithm is tested with a synthetic reference vector.
It can only be used once a `checksum_source` confirming the algorithm has been
recorded in the profile. This field documents the user's verification; the
generator cannot independently prove that the cited source is correct.

## What is needed to enable BMS/motor signals

Each message needs the manufacturer's message map, a DBC or another confirmed
specification: ID and frame type, DLC, bit/byte position and order, signedness,
scale and offset, allowed values, transmission interval and any checksum/counter
algorithm. Confirm at least one known physical value and its corresponding raw
frame with an independent decoder.

Once the information is available, add it to `config/vehicle.json`, regenerate
the profile and test against known reference frames. Signals named `SOC` and
`RPM` automatically enable the corresponding serial command aliases. The code does
not assume that the example API in the task description is the car's confirmed
message map.

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
