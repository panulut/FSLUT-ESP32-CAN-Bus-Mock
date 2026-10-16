#pragma once

// EXAMPLE wiring for a classic ESP32 Dev Module. Check your actual board/module.
namespace board {
constexpr int Sck = 18;
constexpr int Miso = 19;
constexpr int Mosi = 23;
constexpr int Cs = 5;
// INT is not needed: bounded SPI polling is used. Do not connect a 5 V INT to ESP32.
constexpr unsigned OscillatorHz = 16000000;
constexpr unsigned Bitrate = 1000000;
constexpr unsigned SpiHz = 1000000;
constexpr unsigned TxTimeoutMs = 100;
// Listen-only at boot. START/PLAY explicitly enables normal CAN operation.
}
