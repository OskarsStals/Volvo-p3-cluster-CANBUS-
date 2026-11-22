// Volvo P3 instrument cluster (DIM) on the bench: bus wake search.
// BENCH ONLY. Transmits. Never connect to a car.
//
// Goal: find out what wakes the cluster's second CAN bus (U7, normally
// silent and not acknowledging). Every frame is sent once (one-shot) and the
// transmit error counter tells whether anybody acknowledged it. The first
// acknowledged frame is reported, so we know what woke the bus.
//
// Phases, repeated:
//   1. real P3 high-speed snapshot (standard IDs) at the chosen rate
//   2. P2-era 29-bit wake/keep-alive frames (VolvoDIM, unverified on P3)
//   3. sweep of every standard ID 0x000-0x6FF with data 00.. and FF..
//   4. sweep of 29-bit IDs 0x000xxxxx-style is too large; only known ones
// Diagnostic IDs (0x700-0x7FF) are skipped on purpose.
//
// Serial: 1 = 125 kbit/s (default), 5 = 500 kbit/s, s = status

#include <SPI.h>
#include <mcp_can.h>
#include "../simulator/snapshot_xc70_2009.h"

static MCP_CAN *can = nullptr;
static uint8_t rate = CAN_125KBPS;
static unsigned long sent = 0, acked = 0;
static bool reportedWake = false;

static void startCan() {
  can->begin(MCP_ANY, rate, MCP_16MHZ);
  can->setMode(MCP_NORMAL);
  can->enOneShotTX();
}

static void printFrame(const __FlashStringHelper *tag, uint32_t id, bool ext, const uint8_t *d) {
  Serial.print(tag); Serial.print(millis()); Serial.print('\t');
  Serial.print(ext ? F("EXT ") : F("STD ")); Serial.print(id, HEX); Serial.print('\t');
  for (uint8_t i = 0; i < 8; i++) { if (d[i] < 0x10) Serial.print('0'); Serial.print(d[i], HEX); Serial.print(' '); }
  Serial.println();
}

static void drainRx() {
  while (can->checkReceive() == CAN_MSGAVAIL) {
    uint32_t id; uint8_t len; uint8_t buf[8] = {0};
    if (can->readMsgBuf(&id, &len, buf) != CAN_OK) break;
    printFrame(F("RX "), id & 0x1FFFFFFFUL, id & 0x80000000UL, buf);
  }
}

// Send one frame once; returns true if it was acknowledged.
static bool sendOnce(uint32_t id, bool ext, const uint8_t *d) {
  if (can->errorCountTX() >= 96) startCan();
  uint8_t before = can->errorCountTX();
  uint8_t data[8]; memcpy(data, d, 8);
  uint8_t rc = can->sendMsgBuf(id, ext ? 1 : 0, 8, data);
  delayMicroseconds(rate == CAN_125KBPS ? 1500 : 400);
  uint8_t after = can->errorCountTX();
  sent++;
  drainRx();
  bool ok = rc == CAN_OK && after <= before;
  if (ok) {
    acked++;
    if (!reportedWake) { reportedWake = true; printFrame(F("# FIRST ACK after "), id, ext, d); }
  }
  return ok;
}

static void status(const __FlashStringHelper *phase) {
  Serial.print(F("# ")); Serial.print(phase);
  Serial.print(F(": sent ")); Serial.print(sent);
  Serial.print(F(", acked ")); Serial.print(acked);
  Serial.print(F(", TEC ")); Serial.println(can->errorCountTX());
}

static void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '1') { rate = CAN_125KBPS; startCan(); Serial.println(F("# 125 kbit/s")); }
    else if (c == '5') { rate = CAN_500KBPS; startCan(); Serial.println(F("# 500 kbit/s")); }
    else if (c == 's') status(F("status"));
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  Serial.println(F("# Volvo P3 DIM bus wake search (BENCH ONLY, transmits)"));
  const uint8_t candidates[] = {10, 9};
  for (uint8_t c : candidates) {
    pinMode(9, OUTPUT); digitalWrite(9, HIGH);
    pinMode(10, OUTPUT); digitalWrite(10, HIGH);
    MCP_CAN *p = new MCP_CAN(c);
    if (p->begin(MCP_ANY, CAN_125KBPS, MCP_16MHZ) == CAN_OK) { can = p; break; }
    delete p;
  }
  if (!can) { Serial.println(F("# ERROR: MCP2515 not found")); while (true) {} }
  startCan();
  Serial.println(F("# 125 kbit/s. Commands: 1 = 125k, 5 = 500k, s = status"));
}

void loop() {
  // 1. Snapshot replay, 20 rounds.
  for (uint8_t round = 0; round < 20; round++) {
    for (uint8_t i = 0; i < SIM_ID_COUNT; i++) {
      SimId e; memcpy_P(&e, &SIM_IDS[i], sizeof(e));
      uint8_t d[8]; memcpy_P(d, VARIANTS[e.first + (round % e.count)], 8);
      sendOnce(e.id, false, d);
      handleSerial();
    }
  }
  status(F("snapshot"));

  // 2. P2-era 29-bit frames (andrewgabler/VolvoDIM), 50 rounds.
  static const uint32_t P2_IDS[] = {0x000FFFFE, 0x217FFC, 0x2803008, 0x3C01428, 0x381526C,
                                    0x3600008, 0xA10408, 0x2006428, 0x1A0600A, 0x2616CFC,
                                    0x1017FFC, 0x3200408};
  const uint8_t wake[8] = {0xCB, 0x51, 0xB2, 0x02, 0x00, 0x00, 0x00, 0x00};
  const uint8_t ign[8]  = {0x00, 0x00, 0x00, 0x00, 0xD8, 0xF0, 0x58, 0x00};
  const uint8_t zero[8] = {0};
  for (uint8_t round = 0; round < 50; round++) {
    sendOnce(0x000FFFFE, true, wake);
    sendOnce(0x217FFC, true, ign);
    for (uint8_t i = 2; i < sizeof(P2_IDS) / sizeof(P2_IDS[0]); i++) sendOnce(P2_IDS[i], true, zero);
    handleSerial();
  }
  status(F("P2 29-bit"));

  // 3. Standard ID sweep with two data patterns.
  const uint8_t ff[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  for (uint8_t pat = 0; pat < 2; pat++) {
    for (uint16_t id = 0; id < 0x700; id++) {
      sendOnce(id, false, pat ? ff : zero);
      if ((id & 0x3F) == 0) handleSerial();
    }
    status(pat ? F("sweep FF") : F("sweep 00"));
  }
}
