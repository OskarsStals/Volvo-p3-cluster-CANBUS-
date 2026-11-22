// Volvo P3 instrument cluster (DIM) on the bench: stage 2, simulator.
// BENCH ONLY. This sketch transmits continuously. Never connect it to a car.
//
// Replays a snapshot of a real P3 high-speed CAN bus (2009 XC70, engine
// running at idle, see snapshot_xc70_2009.h) at 500 kbit/s to cluster bus 1,
// every PERIOD_MS, and prints any frame the cluster sends back.
//
// Serial commands (115200 baud):
//   g  start sending (on at power-up)
//   x  stop sending
//   s  summary of IDs received from the cluster
//   h  help

#include <SPI.h>
#include <mcp_can.h>
#include "snapshot_xc70_2009.h"

static const uint16_t PERIOD_MS = 50;
static uint8_t variantStep[64];

// RPM override. 0x12A EngineData: RPM = ((b6 & 0x1F) << 8) | b7, 1 rpm/bit
// (matches 705 rpm at idle in the XC70 log and opendbc volvo_v60_2015).
// -1 = leave the logged value. 'w' sweeps 0..6000..0.
static int16_t rpmOverride = -1;
static uint8_t skip405 = 0;   // 0 = send all, 1 = skip VIN parts (10-12), 2 = skip all 0x405
static bool rpmSweep = false;
static unsigned long sweepStart = 0;

static int16_t ign0 = -1, ign1 = -1;   // 0x030 byte0/byte1 override, -1 = logged value
static void patchFrame(uint16_t id, uint8_t *d) {
  if (id == 0x030) { if (ign0 >= 0) d[0] = ign0; if (ign1 >= 0) d[1] = ign1; }
  if (id == 0x12A) {
    int16_t rpm = rpmOverride;
    if (rpmSweep) {
      unsigned long t = (millis() - sweepStart) % 8000;
      rpm = t < 4000 ? (int16_t)(t * 6000UL / 4000) : (int16_t)((8000 - t) * 6000UL / 4000);
    }
    if (rpm >= 0) { d[6] = (d[6] & 0xE0) | ((rpm >> 8) & 0x1F); d[7] = rpm & 0xFF; }
  }
}

static MCP_CAN *can = nullptr;
static bool sending = true;
static unsigned long lastCycle = 0, sentFrames = 0, failedFrames = 0, lastReport = 0;

// IDs present in the 2015 V60 (opendbc volvo_v60_2015_pt) but absent from the
// 2009 XC70 log. Sent with zero data to test whether the 2017 DIM expects them.
static const uint16_t EXTRA_IDS[] PROGMEM = {0x010, 0x020, 0x051, 0x115, 0x133, 0x180, 0x220, 0x235,
  0x246, 0x260, 0x261, 0x262, 0x264, 0x270, 0x288, 0x2EE, 0x31A, 0x322, 0x3FD, 0x40F, 0x4FF};
static bool extraOn = true;
static void drainRx();
static void sendCycle() {
  if (!sending || millis() - lastCycle < PERIOD_MS) return;
  lastCycle = millis();
  for (uint8_t i = 0; i < SIM_ID_COUNT; i++) {
    SimId e;
    memcpy_P(&e, &SIM_IDS[i], sizeof(e));
    uint8_t data[8];
    memcpy_P(data, VARIANTS[e.first + (variantStep[i] % e.count)], 8);
    variantStep[i]++;
    if (e.id == 0x405 && (skip405 == 2 || (skip405 == 1 && data[0] >= 0x10 && data[0] <= 0x12))) continue;
    patchFrame(e.id, data);
    if (can->sendMsgBuf(e.id, 0, 8, data) == CAN_OK) sentFrames++;
    else failedFrames++;
  }
  if (extraOn) {
    for (uint8_t i = 0; i < sizeof(EXTRA_IDS) / 2; i++) {
      uint8_t z[8] = {0};
      if (can->sendMsgBuf(pgm_read_word(&EXTRA_IDS[i]), 0, 8, z) == CAN_OK) sentFrames++;
      else failedFrames++;
    }
  }
}

struct Seen { uint32_t id; uint16_t count; uint8_t last[8]; uint8_t len; };
static Seen seen[24];
static uint8_t seenCount = 0;

static void printFrame(uint32_t id, uint8_t len, const uint8_t *buf) {
  Serial.print(millis()); Serial.print('\t');
  Serial.print((id & 0x80000000UL) ? F("EXT ") : F("STD "));
  Serial.print(id & 0x1FFFFFFFUL, HEX); Serial.print('\t');
  Serial.print(len); Serial.print('\t');
  for (uint8_t i = 0; i < len; i++) {
    if (buf[i] < 0x10) Serial.print('0');
    Serial.print(buf[i], HEX); Serial.print(' ');
  }
  Serial.println();
}

// Print a received frame only when it is new or its data changed, so a busy
// cluster does not flood the serial link.
static void handleRx(uint32_t id, uint8_t len, const uint8_t *buf) {
  for (uint8_t i = 0; i < seenCount; i++) {
    if (seen[i].id != id) continue;
    if (seen[i].count < 65535) seen[i].count++;
    if (seen[i].len == len && memcmp(seen[i].last, buf, len) == 0) return;
    seen[i].len = len; memcpy(seen[i].last, buf, len);
    printFrame(id, len, buf);
    return;
  }
  if (seenCount < sizeof(seen) / sizeof(seen[0])) {
    Seen &s = seen[seenCount++];
    s.id = id; s.count = 1; s.len = len; memcpy(s.last, buf, len);
  }
  Serial.print(F("NEW ")); printFrame(id, len, buf);
}

static void drainRx() {
  while (can->checkReceive() == CAN_MSGAVAIL) {
    uint32_t id; uint8_t len; uint8_t buf[8];
    if (can->readMsgBuf(&id, &len, buf) != CAN_OK) break;
    if (len > 8) len = 8;
    handleRx(id, len, buf);
  }
}

// Read-only diagnostic request to the cluster (request 0x720, reply 0x728).
// Long replies use ISO-TP; a flow-control frame lets the rest follow.
static void diagRequest(const uint8_t *req) {
  uint8_t d[8]; memcpy(d, req, 8);
  Serial.print(F("# DIAG TX ")); printFrame(0x720, 8, d);
  can->sendMsgBuf(0x720, 0, 8, d);
  unsigned long t = millis();
  while (millis() - t < 800) {
    while (can->checkReceive() == CAN_MSGAVAIL) {
      uint32_t id; uint8_t len; uint8_t buf[8];
      if (can->readMsgBuf(&id, &len, buf) != CAN_OK) break;
      if (len > 8) len = 8;
      if ((id & 0x1FFFFFFFUL) != 0x728) { handleRx(id, len, buf); continue; }
      Serial.print(F("# DIAG RX ")); printFrame(id, len, buf);
      if ((buf[0] & 0xF0) == 0x10) {
        uint8_t fc[8] = {0x30, 0, 0x14, 0, 0, 0, 0, 0};   // STmin 20 ms so serial keeps up
        can->sendMsgBuf(0x720, 0, 8, fc);
      }
    }
  }
}


static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}
static uint16_t readHex16() {
  uint16_t v = 0; unsigned long t = millis();
  while (millis() - t < 2000) {
    if (!Serial.available()) continue;
    char c = Serial.read(); int h = hexVal(c);
    if (h < 0) { if (c == ' ' || c == 10 || c == 13) break; continue; }
    v = (v << 4) | h; t = millis();
  }
  return v;
}
// Read every DID in [a, b] (22 xx xx) while the simulation keeps running.
// Prints positive replies only (first frame and continuations).
static void didScan(uint16_t a, uint16_t b) {
  Serial.print(F("# SCAN ")); Serial.print(a, HEX); Serial.print('-'); Serial.println(b, HEX);
  for (uint32_t did = a; did <= b; did++) {
    uint8_t r[8] = {0x03, 0x22, (uint8_t)(did >> 8), (uint8_t)did, 0, 0, 0, 0};
    can->sendMsgBuf(0x720, 0, 8, r);
    unsigned long t = millis(); bool done = false, multi = false;
    while (millis() - t < (multi ? 600 : 120) && !done) {
      sendCycle();
      while (can->checkReceive() == CAN_MSGAVAIL) {
        uint32_t id; uint8_t len; uint8_t buf[8];
        if (can->readMsgBuf(&id, &len, buf) != CAN_OK) break;
        if ((id & 0x1FFFFFFFUL) != 0x728) continue;
        uint8_t pci = buf[0] & 0xF0;
        if (pci == 0x00) { if (buf[1] == 0x62) { Serial.print(F("DID ")); printFrame(id, 8, buf); } if (!(buf[1] == 0x7F && buf[3] == 0x78)) done = true; }
        else if (pci == 0x10) { Serial.print(F("DID ")); printFrame(id, 8, buf); multi = true; t = millis();
          uint8_t fc[8] = {0x30, 0, 0x14, 0, 0, 0, 0, 0}; can->sendMsgBuf(0x720, 0, 8, fc); }
        else if (pci == 0x20) { Serial.print(F("DID ")); printFrame(id, 8, buf); t = millis(); }
      }
    }
  }
  Serial.println(F("# SCAN done"));
}

static void printHelp() {
  Serial.println(F("# Commands: g = start, x = stop, s = summary, d/D = DTCs, r<rpm>, w = RPM sweep, h = help"));
}

static void printSummary() {
  Serial.print(F("# Sent ")); Serial.print(sentFrames);
  Serial.print(F(", failed ")); Serial.print(failedFrames);
  Serial.print(F(", TEC ")); Serial.print(can->errorCountTX());
  Serial.print(F(", REC ")); Serial.println(can->errorCountRX());
  Serial.print(F("# IDs from cluster: ")); Serial.println(seenCount);
  for (uint8_t i = 0; i < seenCount; i++) {
    Serial.print(F("#  0x")); Serial.print(seen[i].id & 0x1FFFFFFFUL, HEX);
    Serial.print(F(" x")); Serial.println(seen[i].count);
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  Serial.println(F("# Volvo P3 DIM simulator (BENCH ONLY, transmits)"));
  const uint8_t candidates[] = {10, 9};
  for (uint8_t c : candidates) {
    pinMode(9, OUTPUT); digitalWrite(9, HIGH);
    pinMode(10, OUTPUT); digitalWrite(10, HIGH);
    MCP_CAN *probe = new MCP_CAN(c);
    if (probe->begin(MCP_ANY, CAN_500KBPS, MCP_16MHZ) == CAN_OK) { can = probe; break; }
    delete probe;
  }
  if (!can) { Serial.println(F("# ERROR: MCP2515 not found")); while (true) {} }
  can->setMode(MCP_NORMAL);
  Serial.print(F("# 500 kbit/s, replaying ")); Serial.print(SIM_ID_COUNT);
  Serial.print(F(" IDs every ")); Serial.print(PERIOD_MS); Serial.println(F(" ms"));
  printHelp();
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'g') { sending = true; Serial.println(F("# sending")); }
    else if (c == 'x') { sending = false; Serial.println(F("# stopped")); }
    else if (c == 's') printSummary();
    else if (c == '7' || c == '8') {
      // Stop sending and switch to receive-only at 125k ('7') or 500k ('8'),
      // in normal mode so the cluster's frames get ACKed. Nothing is sent.
      sending = false;
      can->begin(MCP_ANY, c == '7' ? CAN_125KBPS : CAN_500KBPS, MCP_16MHZ);
      can->setMode(MCP_NORMAL);
      seenCount = 0;
      Serial.println(c == '7' ? F("# RX only, 125 kbit/s") : F("# RX only, 500 kbit/s"));
    }
    else if (c == 'v') { skip405 = (skip405 + 1) % 3; Serial.print(F("# 0x405 mode ")); Serial.println(skip405); }
    else if (c == 'w') { rpmSweep = !rpmSweep; sweepStart = millis(); Serial.println(rpmSweep ? F("# RPM sweep on") : F("# RPM sweep off")); }
    else if (c == 'r') {   // r<number>  e.g. r3000 ; r alone = logged value
      long v = Serial.parseInt();
      rpmOverride = v > 0 ? (int16_t)v : -1; rpmSweep = false;
      Serial.print(F("# RPM override ")); Serial.println(rpmOverride);
    }
    else if (c == 'd') {   // DTCs, all status bits
      const uint8_t r[8] = {0x03, 0x19, 0x02, 0xFF, 0, 0, 0, 0};
      diagRequest(r);
    } else if (c == 'e') { // E103: rejected configuration items
      const uint8_t r[8] = {0x03, 0x22, 0xE1, 0x03, 0, 0, 0, 0}; diagRequest(r);
    } else if (c == 'o') { // odometer-like DIDs 61BB, D935, DD01
      const uint8_t a[8] = {0x03, 0x22, 0x61, 0xBB, 0, 0, 0, 0}; diagRequest(a);
      const uint8_t b[8] = {0x03, 0x22, 0xD9, 0x35, 0, 0, 0, 0}; diagRequest(b);
      const uint8_t d[8] = {0x03, 0x22, 0xDD, 0x01, 0, 0, 0, 0}; diagRequest(d);
    } else if (c == 'S') { // S<start> <end> in hex, e.g. SD900 DAFF
      uint16_t a = readHex16(), b = readHex16(); didScan(a, b);
    } else if (c == 'i') { // i<b0> <b1> hex, e.g. i78 07 ; overrides 0x030 bytes 0-1
      ign0 = readHex16(); ign1 = readHex16();
      Serial.print(F("# 0x030 = ")); Serial.print(ign0, HEX); Serial.print(' '); Serial.println(ign1, HEX);
    } else if (c == 'm') { extraOn = !extraOn; Serial.println(extraOn ? F("# extra V60 IDs on") : F("# extra V60 IDs off"));
    } else if (c == 'c') { // clear all DTCs (diagnostic memory only)
      const uint8_t r[8] = {0x04, 0x14, 0xFF, 0xFF, 0xFF, 0, 0, 0}; diagRequest(r);
    } else if (c == 'D') { // DTCs that are failed now / confirmed
      const uint8_t r[8] = {0x03, 0x19, 0x02, 0x09, 0, 0, 0, 0};
      diagRequest(r);
    }
    else if (c == 'h' || c == '?') printHelp();
  }

  sendCycle();
  drainRx();

  if (millis() - lastReport >= 5000) { lastReport = millis(); printSummary(); }
}
