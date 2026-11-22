// Volvo P3 instrument cluster (DIM) on the bench: stage 1, sniffer and bit-rate probe.
// Hardware: Arduino Uno + MCP2515 CAN shield (16 MHz crystal).
//
// Wiring for DIM 31453090 (measured, see docs/pinout.md):
//   A7 = +12 V, A1 = ground
//   CAN bus 1 (transceiver U6): B4 = CAN-H, B5 = CAN-L
//   CAN bus 2 (transceiver U7): B3 = CAN-H, A2 = CAN-L
//   Arduino GND must be tied to DIM ground (A1).
//
// On start the sketch only listens (transmits nothing, does not ACK) and steps
// through bit rates every 5 s while the bus is silent.
//
// Serial commands (115200 baud):
//   1 / 2 / 5  bit rate 125 / 250 / 500 kbit/s
//   a          auto-step bit rate while the bus is silent (on at start)
//   l          listen-only (no transmit, no ACK)
//   n          normal mode: ACK received frames, but send no data
//   p          bit-rate probe: at each rate send a harmless test frame and check
//              whether the cluster acknowledges it. Finds the rate even when
//              the cluster itself stays silent.
//   s          summary of IDs seen
//   h          help
//
// Output format, one frame per line (tab separated):
//   <millis>  <STD|EXT> <id hex>  <dlc>  <data bytes hex>

#include <SPI.h>
#include <mcp_can.h>

static MCP_CAN *can = nullptr;
static uint8_t csPin = 0;

static const uint8_t SPEEDS[] = {CAN_125KBPS, CAN_500KBPS, CAN_250KBPS};
static const char *SPEED_NAMES[] = {"125", "500", "250"};
static const uint8_t SPEED_COUNT = sizeof(SPEEDS) / sizeof(SPEEDS[0]);
// CAR_LISTEN_ONLY build (env "car"): for tapping a running car. Fixed at
// 500 kbit/s, listen-only, and every command that could transmit is
// disabled, so the MCP2515 never drives the car's bus, not even with ACKs.
#ifdef CAR_LISTEN_ONLY
static uint8_t speedIdx = 1;      // 500 kbit/s
static bool autoSpeed = false;
#else
static uint8_t speedIdx = 0;
static bool autoSpeed = true;
#endif
static bool normalMode = false;

static unsigned long lastFrameMs = 0;
static unsigned long modeStartMs = 0;

// Test frame for the bit-rate probe: standard ID 0x7F0, all zeros. The cluster
// does not know this ID, so it only acknowledges it and otherwise ignores it.
static const uint32_t PROBE_ID = 0x7F0;

struct Seen { uint32_t id; uint16_t count; uint8_t dlc; };
static Seen seen[40];
static uint8_t seenCount = 0;

static bool startCan(uint8_t speed, bool normal) {
#ifdef CAR_LISTEN_ONLY
  speed = CAN_500KBPS;
  normal = false;
#endif
  if (can->begin(MCP_ANY, speed, MCP_16MHZ) != CAN_OK) return false;
  can->setMode(normal ? MCP_NORMAL : MCP_LISTENONLY);
  return true;
}

static void applySpeed() {
  if (startCan(SPEEDS[speedIdx], normalMode)) {
    Serial.print(F("# Bit rate ")); Serial.print(SPEED_NAMES[speedIdx]);
    Serial.print(F(" kbit/s, mode: "));
    Serial.println(normalMode ? F("normal (ACK)") : F("listen-only"));
  } else {
    Serial.println(F("# ERROR: MCP2515 not responding"));
  }
  modeStartMs = millis();
}

static void remember(uint32_t id, uint8_t dlc) {
  for (uint8_t i = 0; i < seenCount; i++) {
    if (seen[i].id == id) { if (seen[i].count < 65535) seen[i].count++; return; }
  }
  if (seenCount < sizeof(seen) / sizeof(seen[0])) seen[seenCount++] = {id, 1, dlc};
}

static void printSummary() {
  Serial.print(F("# IDs seen: ")); Serial.println(seenCount);
  for (uint8_t i = 0; i < seenCount; i++) {
    bool ext = seen[i].id & 0x80000000UL;
    Serial.print(ext ? F("#  EXT 0x") : F("#  STD 0x"));
    Serial.print(seen[i].id & 0x1FFFFFFFUL, HEX);
    Serial.print(F("  dlc=")); Serial.print(seen[i].dlc);
    Serial.print(F("  count=")); Serial.println(seen[i].count);
  }
}

static void printHelp() {
  Serial.println(F("# Commands: 1/2/5 = 125/250/500 kbit/s, a = auto bit rate,"));
  Serial.println(F("#   l = listen-only, n = normal (ACK), p = bit-rate probe,"));
  Serial.println(F("#   s = IDs seen, d = diag ping 0x7DF, D = diag sweep 0x700-0x7EF,"));
  Serial.println(F("#   h = help"));
}

static void printFrame(uint32_t id, uint8_t len, const uint8_t *buf) {
  Serial.print(millis()); Serial.print('\t');
  bool ext = id & 0x80000000UL;
  bool rtr = id & 0x40000000UL;
  Serial.print(ext ? F("EXT ") : F("STD "));
  Serial.print(id & 0x1FFFFFFFUL, HEX);
  Serial.print('\t'); Serial.print(len); Serial.print('\t');
  if (rtr) { Serial.println(F("RTR")); return; }
  for (uint8_t i = 0; i < len; i++) {
    if (buf[i] < 0x10) Serial.print('0');
    Serial.print(buf[i], HEX); Serial.print(' ');
  }
  Serial.println();
}

// Read and print every pending frame.
static void drainRx() {
  while (can->checkReceive() == CAN_MSGAVAIL) {
    uint32_t id; uint8_t len; uint8_t buf[8];
    if (can->readMsgBuf(&id, &len, buf) != CAN_OK) break;
    if (len > 8) len = 8;
    lastFrameMs = millis();
    remember(id, len);
    printFrame(id, len, buf);
  }
}

// Bit-rate probe. At each rate the test frame is sent once (one-shot, no
// retransmission) and the transmit error counter (TEC) is compared: an
// unacknowledged frame raises it by 8, an acknowledged one does not.
// A frame that never leaves the chip (bus never idle, for example held
// dominant) also leaves TEC unchanged, so it is counted separately as
// "not sent". 1000 kbit/s is a control rate: a P3 cluster is not expected to
// use it, so ACKs there mean the probe result cannot be trusted.
static const uint8_t PROBE_SPEEDS[] = {CAN_125KBPS, CAN_500KBPS, CAN_250KBPS, CAN_1000KBPS};
static const char *PROBE_NAMES[] = {"125", "500", "250", "1000 (control)"};

static void probeSpeeds() {
  Serial.println(F("# Bit-rate probe: sending test frame 0x7F0 at each rate"));
  uint8_t bestIdx = 255, bestOk = 0;
  bool controlAcked = false;
  uint8_t data[8] = {0};
  for (uint8_t s = 0; s < sizeof(PROBE_SPEEDS); s++) {
    if (!startCan(PROBE_SPEEDS[s], true)) { Serial.println(F("# ERROR: MCP2515 not responding")); return; }
    can->enOneShotTX();
    uint8_t ok = 0, noAck = 0, notSent = 0, tries = 20;
    for (uint8_t t = 0; t < tries; t++) {
      // Above 127 the MCP2515 goes error-passive and stops counting ACK errors,
      // so reset the chip (which clears the counter) before that happens.
      if (can->errorCountTX() >= 96) { startCan(PROBE_SPEEDS[s], true); can->enOneShotTX(); }
      uint8_t before = can->errorCountTX();
      uint8_t rc = can->sendMsgBuf(PROBE_ID, 0, 8, data);
      delay(5);
      uint8_t after = can->errorCountTX();
      if (rc != CAN_OK) notSent++;
      else if (after > before) noAck++;
      else ok++;
      drainRx();
      delay(45);
    }
    Serial.print(F("#  ")); Serial.print(PROBE_NAMES[s]); Serial.print(F(" kbit/s: acknowledged "));
    Serial.print(ok); Serial.print('/'); Serial.print(tries);
    Serial.print(F(", no ACK ")); Serial.print(noAck);
    Serial.print(F(", not sent ")); Serial.print(notSent);
    Serial.print(F(", TEC ")); Serial.print(can->errorCountTX());
    Serial.print(F(", REC ")); Serial.print(can->errorCountRX());
    Serial.print(F(", EFLG 0x")); Serial.println(can->getError(), HEX);
    if (s == 3) { controlAcked = ok >= 5; continue; }
    if (ok > bestOk) { bestOk = ok; bestIdx = s; }
  }
  if (controlAcked) {
    Serial.println(F("# WARNING: the control rate was 'acknowledged' too. The probe result"));
    Serial.println(F("# is not trustworthy (bus may be held dominant). Check bus voltages."));
    normalMode = false;
  } else if (bestIdx != 255 && bestOk >= 15) {
    speedIdx = bestIdx;
    Serial.print(F("# Cluster answers at ")); Serial.print(SPEED_NAMES[speedIdx]);
    Serial.println(F(" kbit/s. Staying at this rate in normal (ACK) mode."));
    autoSpeed = false;
    normalMode = true;
  } else {
    Serial.println(F("# No rate was acknowledged. Check wiring, power and common ground,"));
    Serial.println(F("# or move the wires to the other CAN pair."));
    normalMode = false;
  }
  applySpeed();   // back to regular (retransmitting) operation
}

static void handleCommand(char c);

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  Serial.println(F("# Volvo P3 DIM sniffer"));
#ifdef CAR_LISTEN_ONLY
  Serial.println(F("# CAR build: 500 kbit/s, listen-only, transmit disabled"));
#endif

  // Keep both possible chip-select pins high so the SD card stays off the SPI bus.
  const uint8_t candidates[] = {10, 9};
  for (uint8_t c : candidates) {
    pinMode(9, OUTPUT); digitalWrite(9, HIGH);
    pinMode(10, OUTPUT); digitalWrite(10, HIGH);
    MCP_CAN *probe = new MCP_CAN(c);
    if (probe->begin(MCP_ANY, CAN_125KBPS, MCP_16MHZ) == CAN_OK) {
      can = probe; csPin = c; break;
    }
    delete probe;
  }
  if (!can) {
    Serial.println(F("# ERROR: MCP2515 not found on D10 or D9. Check the shield."));
    while (true) {}
  }
  Serial.print(F("# MCP2515 found, CS = D")); Serial.println(csPin);
#ifdef CAR_LISTEN_ONLY
  handleCommand('h');
#else
  printHelp();
#endif
  applySpeed();
}

// Diagnostic ping. Sends the read-only UDS request "DiagnosticSessionControl,
// default session" (02 10 01) and prints whatever answers. It changes nothing
// in the cluster. 'd' sends it once to the functional address 0x7DF,
// 'D' sends it to every physical address 0x700-0x7EF in turn.
static void sendDiag(uint16_t id) {
  uint8_t req[8] = {0x02, 0x10, 0x01, 0, 0, 0, 0, 0};
  can->sendMsgBuf(id, 0, 8, req);
  unsigned long t = millis();
  while (millis() - t < 40) drainRx();
}

static void diagPing(bool sweep) {
  if (!normalMode) { normalMode = true; autoSpeed = false; applySpeed(); }
  if (!sweep) {
    Serial.println(F("# Diag ping: 0x7DF 02 10 01"));
    sendDiag(0x7DF);
  } else {
    Serial.println(F("# Diag sweep: 02 10 01 to 0x700-0x7EF"));
    for (uint16_t id = 0x700; id < 0x7F0; id++) sendDiag(id);
  }
  unsigned long t = millis();
  while (millis() - t < 300) drainRx();
  Serial.println(F("# Diag done"));
}

// Raw request: "x <id hex> <bytes hex>" + newline, e.g. "x 720 0322F190".
// Sends one 8-byte frame (padded with 00) and prints replies for 1 s. If a
// reply is an ISO-TP first frame, a flow-control frame (30 00 00) is sent to
// the same request ID so the rest of a long answer follows.
static uint8_t hexNib(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 255;
}

static void rawRequest() {
  char line[48]; uint8_t n = 0; unsigned long t = millis();
  while (millis() - t < 2000 && n < sizeof(line) - 1) {
    if (!Serial.available()) continue;
    char c = Serial.read();
    if (c == 10 || c == 13) break;
    line[n++] = c;
  }
  line[n] = 0;
  uint16_t id = 0; uint8_t i = 0;
  while (line[i] == ' ') i++;
  while (hexNib(line[i]) != 255) id = (id << 4) | hexNib(line[i++]);
  uint8_t data[8] = {0}, len = 0;
  while (line[i] && len < 8) {
    if (line[i] == ' ') { i++; continue; }
    uint8_t hi = hexNib(line[i]), lo = hexNib(line[i + 1]);
    if (hi == 255 || lo == 255) break;
    data[len++] = (hi << 4) | lo; i += 2;
  }
  if (!normalMode) { normalMode = true; autoSpeed = false; applySpeed(); }
  Serial.print(F("# TX ")); printFrame(id, 8, data);
  can->sendMsgBuf(id, 0, 8, data);
  t = millis();
  while (millis() - t < 1000) {
    while (can->checkReceive() == CAN_MSGAVAIL) {
      uint32_t rid; uint8_t rlen; uint8_t buf[8];
      if (can->readMsgBuf(&rid, &rlen, buf) != CAN_OK) break;
      if (rlen > 8) rlen = 8;
      lastFrameMs = millis(); remember(rid, rlen); printFrame(rid, rlen, buf);
      if ((buf[0] & 0xF0) == 0x10) {
        uint8_t fc[8] = {0x30, 0, 0, 0, 0, 0, 0, 0};
        can->sendMsgBuf(id, 0, 8, fc);
      }
    }
  }
  Serial.println(F("# RX done"));
}

// DID scan: "S<start hex> <end hex>" + newline, e.g. "SF100 F1FF".
// Sends ReadDataByIdentifier (22 xx xx, read-only) to 0x720 for every DID in
// the range and prints only positive answers (all ISO-TP frames).
static uint32_t readHexToken() {
  uint32_t v = 0; unsigned long t = millis(); bool any = false;
  while (millis() - t < 2000) {
    if (!Serial.available()) continue;
    char c = Serial.peek();
    uint8_t n = hexNib(c);
    if (n == 255) { if (any) break; Serial.read(); continue; }
    Serial.read(); v = (v << 4) | n; any = true;
  }
  return v;
}

static void didScan() {
  uint16_t a = readHexToken(), b = readHexToken();
  if (!normalMode) { normalMode = true; autoSpeed = false; applySpeed(); }
  Serial.print(F("# DID scan 0x")); Serial.print(a, HEX); Serial.print(F("-0x")); Serial.println(b, HEX);
  uint16_t found = 0;
  for (uint32_t did = a; did <= b; did++) {
    if ((did & 0xFF) == 0) { Serial.print(F("# at 0x")); Serial.println(did, HEX); }
    uint8_t req[8] = {0x03, 0x22, (uint8_t)(did >> 8), (uint8_t)did, 0, 0, 0, 0};
    can->sendMsgBuf(0x720, 0, 8, req);
    unsigned long t = millis(), limit = 30;
    bool positive = false;
    while (millis() - t < limit) {
      if (can->checkReceive() != CAN_MSGAVAIL) continue;
      uint32_t id; uint8_t len; uint8_t buf[8];
      if (can->readMsgBuf(&id, &len, buf) != CAN_OK) break;
      if ((id & 0x1FFFFFFFUL) != 0x728) continue;
      uint8_t pci = buf[0] & 0xF0;
      if (pci == 0x00 && buf[1] == 0x7F) {
        if (buf[3] == 0x78) { limit = millis() - t + 500; continue; }   // pending
        break;                                                           // negative
      }
      if (!positive) { positive = true; found++; Serial.print(F("DID ")); Serial.println(did, HEX); }
      printFrame(id, len, buf);
      if (pci == 0x10) {
        uint8_t fc[8] = {0x30, 0, 0, 0, 0, 0, 0, 0};
        can->sendMsgBuf(0x720, 0, 8, fc);
        limit = millis() - t + 300;
      }
      if (pci == 0x00) break;
    }
  }
  Serial.print(F("# DID scan done, found ")); Serial.println(found);
}

static void handleCommand(char c) {
#ifdef CAR_LISTEN_ONLY
  // Only harmless commands; anything else is ignored.
  if (c == 's') printSummary();
  else if (c == 'f') {
    // Receive filter: only 0x400 and 0x405 (configuration messages), so none
    // are lost on the serial link. Filters only affect reception.
    can->init_Mask(0, 0, 0x07FF0000UL);
    can->init_Filt(0, 0, 0x04000000UL);
    can->init_Filt(1, 0, 0x04050000UL);
    can->init_Mask(1, 0, 0x07FF0000UL);
    for (uint8_t i = 2; i < 6; i++) can->init_Filt(i, 0, 0x04000000UL);
    can->setMode(MCP_LISTENONLY);
    Serial.println(F("# Filter on: 0x400 and 0x405 only"));
  }
  else if (c == 'h' || c == '?') {
    Serial.println(F("# CAR build: 500 kbit/s listen-only, transmit disabled."));
    Serial.println(F("# Commands: s = IDs seen, f = only 0x400/0x405, h = help, other = marker"));
  } else if (c != 10 && c != 13) {
    Serial.print(F("# MARK ")); Serial.print(millis()); Serial.print(' '); Serial.println(c);
  }
  return;
#endif
  switch (c) {
    case '1': speedIdx = 0; autoSpeed = false; applySpeed(); break;
    case '5': speedIdx = 1; autoSpeed = false; applySpeed(); break;
    case '2': speedIdx = 2; autoSpeed = false; applySpeed(); break;
    case 'a': autoSpeed = true; Serial.println(F("# Auto bit rate on")); break;
    case 'l': normalMode = false; applySpeed(); break;
    case 'n': normalMode = true; applySpeed(); break;
    case 'p': probeSpeeds(); break;
    case 's': printSummary(); break;
    case 'x': rawRequest(); break;
    case 'S': didScan(); break;
    case 'd': diagPing(false); break;
    case 'D': diagPing(true); break;
    case 'h': case '?': printHelp(); break;
    default: break;
  }
}

void loop() {
  while (Serial.available()) handleCommand((char)Serial.read());
  drainRx();

  if (autoSpeed && millis() - modeStartMs > 5000 && millis() - lastFrameMs > 5000) {
    speedIdx = (speedIdx + 1) % SPEED_COUNT;
    applySpeed();
  }
}
