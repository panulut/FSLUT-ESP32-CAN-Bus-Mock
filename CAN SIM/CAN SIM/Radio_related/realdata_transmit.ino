#include <SPI.h>
#include <LoRa.h>
#include <Arduino.h>

#define LORA_SCK  18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_SS   5

#define LORA_BAND 433E6

SPIClass loraSPI(VSPI);

// Just for testing: replay recorded CAN-like frames and send them over LoRa.
// Prints transmitted frames to Serial.

struct ReplayFrame {
  uint32_t id;
  uint8_t data[8];
};

const ReplayFrame tests[] = {
#include "replay_4500_5000.inc"
};

const size_t testCount = sizeof(tests) / sizeof(tests[0]);
size_t currentIndex = 0;

constexpr uint32_t REPLAY_INTERVAL_MS = 60;
uint32_t lastReplayMs = 0;

void writeU32LE(uint8_t* b, uint32_t v) {
  b[0] = (uint8_t)(v & 0xFF);
  b[1] = (uint8_t)((v >> 8) & 0xFF);
  b[2] = (uint8_t)((v >> 16) & 0xFF);
  b[3] = (uint8_t)((v >> 24) & 0xFF);
}

// non-blocking LoRa send (4 byte ID, 1 byte DLC, 8 byte data)
void sendCanOverLoRa(uint32_t id, uint8_t dlc, const uint8_t data[8]) {
  uint8_t pkt[13];
  writeU32LE(pkt, id);
  pkt[4] = dlc;
  memcpy(pkt + 5, data, 8);

  // endPacket(false) is blocking, so TX is always done before we get here.
  LoRa.idle();

  if (LoRa.beginPacket() == 0) {
    return;
  }
  LoRa.write(pkt, sizeof(pkt));
  LoRa.endPacket(false);
}

bool initLoRaWithRetry() {
  for (int attempt = 1; attempt <= 5; attempt++) {
    if (LoRa.begin(LORA_BAND)) {
      return true;
    }
    Serial.print("LoRa init retry ");
    Serial.println(attempt);
    delay(300);
  }

  return false;
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("LoRa Real-Data Transmitter");

  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setSPI(loraSPI);
  LoRa.setPins(LORA_SS, -1, -1);

  if (!initLoRaWithRetry()) {
    Serial.println("Starting LoRa failed!");
    while (true) {
      delay(2000);
    }
  }

  Serial.println("LoRa init succeeded.");
  LoRa.setTxPower(17);
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(500E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();

  Serial.print("Replay frame count: ");
  Serial.println((int)testCount);
}

void loop() {
  const ReplayFrame& frame = tests[currentIndex];

  // Keep transmitter console output in the same style as CAN RX logging.
  Serial.print("RX ID=0x");
  Serial.print(frame.id, HEX);
  Serial.print(" DLC=");
  Serial.print(8);
  Serial.print(" Data=");
  for (int i = 0; i < 8; i++) {
    if (frame.data[i] < 16) Serial.print("0");
    Serial.print(frame.data[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  // Transmit replay frame over LoRa
  sendCanOverLoRa(frame.id, 8, frame.data);

  currentIndex++;
  if (currentIndex >= testCount) {
    currentIndex = 0;
  }

  delay(10);
}
