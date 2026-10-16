# Task ESP32 CAN Bus Mock 

## Objective

Develop a **configurable CAN bus simulator running on an ESP32 with a CAN controller/transceiver**, capable of reproducing vehicle CAN communication for testing other embedded systems without the actual car.

The simulator must support:
- Pre-recorded CAN message playback from CSV/TXT files.
- Programmatically generated CAN messages with configurable values.
- Live configuration and monitoring through USB Serial.
- Simultaneous CAN transmission and reception.

**CAN** chips we use : `MCP2515 + TJA1051` and 

## Requirements

### 1. CAN Message Simulation

Implement the **green-flagged CAN messages** from Excel

Each message should have:
- Default values for all defined signals.
- Configurable transmission interval.
- Ability to update individual fields or the entire message.
- Correct CAN ID, payload length, encoding, and scaling.
- Continuous cyclic transmission.

Heartbeats, fault messages, and error codes will be added later. Keep the architecture expandable.

### 2. Message Configuration API

Create a simple interface for modifying simulated vehicle data while the simulator is running.

**Example API:**

```cpp
canMock.begin(1000000);        // CAN bitrate: 1Mbit/s
                              // Or any other Bitrate
canMock.start();              // Start cyclic transmission

// Update individual signals
canMock.set6B0_4(50);         // Update field 4 of 0x6B0
canMock.setBatterySOC(75.5);

// Update complete CAN messages
canMock.set_301(
    350.0,  // Capacitor voltage
    345.0,  // Battery voltage
    20.0,   // Battery current
    15.0    // Target torque
);

// Control
canMock.stop();
canMock.setInterval(0x301, 100);  // 100 ms
```

API names are suggestions.

### 3. Pre-Recorded CAN Playback

Implement a method for loading predefined CAN traffic into the firmware.

Create a **PC-side Python script** that:
1. Reads a CSV or TXT file containing CAN messages.
2. Validates message IDs, DLC, payloads, and timestamps.
3. Converts the messages into a C/C++ `.inc` file.
4. Allows the generated file to be included and compiled into ESP32 firmware.

**Example input CSV:**

```csv
timestamp_ms,can_id,dlc,data
0,0x301,8,00 10 20 30 40 50 60 70
10,0x6B0,8,01 02 03 04 05 06 07 08
20,0x301,8,00 11 21 31 41 51 61 71
```

**Example input txt:**
ID, Time, (PDO/TPDO/NMT - Doesnt matter), Messages
```txt
    0x0128  2144398  PDO   { 0x00 0x00 0x00 0x00 0x00 0x00 0xFF 0x03 }
    0x0297  2144400  PDO   { 0xD4 0x7D 0xFF 0x00 0x25 0x02 0x1C 0x00 }
    0x0301  2144402  PDO   { 0x0D 0x05 0x16 0x00 0x00 0x00 0x00 }
    0x0298  2144405  PDO   { 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 }
    0x0154  2144593  PDO   { 0x00 0x00 0x00 0x00 0xDE 0xFF 0xE2 0xFF }
    0x0128  2144596  PDO   { 0x00 0x00 0x00 0x00 0x00 0x00 0xFF 0x03 }

```

**Example generated file:**

```cpp
// generated_can_data.inc

const CANFrame recordedMessages[] = {
    {0x301, {0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70}},
    {0x6B0, {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}},
    {0x301, {0x00, 0x11, 0x21, 0x31, 0x41, 0x51, 0x61, 0x71}}
};
```

Playback requirements:
- Preserve message order and relative timing.
- Support looping playback.
- Allow starting, stopping, and restarting.
- No blocking delays during playback.
- Support large datasets within available ESP32 memory.

### 4. State Machine / Automatic Simulation

Implement a state-based simulation mode that continuously generates CAN messages using internal signal values.

- Maintain default values for all implemented CAN signals.
- Transmit messages at configurable intervals.
- Update signals dynamically without restarting the simulator.
- Allow basic automatic behavior such as RPM ramps, battery discharge, and changing current.
- Keep CAN message generation separate from CAN transmission.

### 5. USB Serial Interface

Since the ESP32 remains connected to a PC during testing, implement a **Serial Monitor command interface**.

Example commands:

```text
START
STOP
STATUS

SET 0x6B0 4 50
SET_SOC 75.5
SET_RPM 3500

INTERVAL 0x301 100

MODE AUTO
MODE MANUAL
MODE PLAYBACK

PLAY
PAUSE
RESTART

MONITOR ON
MONITOR OFF
```

The interface should:
- Accept commands while simulation is running.
- Display confirmation or meaningful errors.
- Allow modifying CAN signals in real time.
- Show current simulation mode and transmitted message count.
- Avoid blocking CAN communication.

### 6. CAN Reception & Monitoring

The ESP32 must also receive CAN messages and display them in the Serial Monitor.

Example output:

```text
[1250 ms] RX 0x301 DLC:8 DATA: 00 10 20 30 40 50 60 70
[1260 ms] RX 0x6B0 DLC:8 DATA: 01 02 03 04 05 06 07 08
[1270 ms] TX 0x301 DLC:8 DATA: 00 11 21 31 41 51 61 71
```

Implement:
- Live CAN message monitoring.
- Optional CAN ID filtering.
- Ability to enable/disable RX and TX monitoring separately.
- Display message timestamps, IDs, DLC, and payloads.
- Prevent slow Serial output from blocking CAN processing.



## Deliverables

- ESP32 CAN simulator firmware.
- Configurable CAN message definitions and default values.
- Python CSV/TXT → `.inc` converter.
- Serial command interface.
- README with setup, commands, and configuration.



## Important Notes

- **Use only the green-flagged messages initially.** .
- Checksum algorithms must be verified rather than guessed.
- The simulator is intended for **bench testing only**.