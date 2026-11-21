// Volvo P3 instrument cluster (DIM) on the bench: stage 1, sniffer and bit-rate probe.
// Hardware: Arduino Uno + MCP2515 CAN shield (16 MHz crystal).
//
// Wiring for DIM 31453090 (measured, see docs/pinout.md):
//   B7 = +12 V, A1 = ground
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
static uint8_t speedIdx = 0;
static bool autoSpeed = true;
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
  Serial.println(F("#   s = IDs seen, h = help"));
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
static void probeSpeeds() {
  Serial.println(F("# Bit-rate probe: sending test frame 0x7F0 at each rate"));
  uint8_t bestIdx = 255, bestOk = 0;
  uint8_t data[8] = {0};
  for (uint8_t s = 0; s < SPEED_COUNT; s++) {
    if (!startCan(SPEEDS[s], true)) { Serial.println(F("# ERROR: MCP2515 not responding")); return; }
    can->enOneShotTX();
    uint8_t ok = 0, tries = 20;
    for (uint8_t t = 0; t < tries; t++) {
      // Above 127 the MCP2515 goes error-passive and stops counting ACK errors,
      // so reset the chip (which clears the counter) before that happens.
      if (can->errorCountTX() >= 96) { startCan(SPEEDS[s], true); can->enOneShotTX(); }
      uint8_t before = can->errorCountTX();
      can->sendMsgBuf(PROBE_ID, 0, 8, data);
      delay(5);
      uint8_t after = can->errorCountTX();
      if (after <= before) ok++;
      drainRx();
      delay(45);
    }
    Serial.print(F("#  ")); Serial.print(SPEED_NAMES[s]); Serial.print(F(" kbit/s: acknowledged "));
    Serial.print(ok); Serial.print('/'); Serial.println(tries);
    if (ok > bestOk) { bestOk = ok; bestIdx = s; }
  }
  if (bestIdx != 255 && bestOk >= 15) {
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

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  Serial.println(F("# Volvo P3 DIM sniffer"));

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
  printHelp();
  applySpeed();
}

static void handleCommand(char c) {
  switch (c) {
    case '1': speedIdx = 0; autoSpeed = false; applySpeed(); break;
    case '5': speedIdx = 1; autoSpeed = false; applySpeed(); break;
    case '2': speedIdx = 2; autoSpeed = false; applySpeed(); break;
    case 'a': autoSpeed = true; Serial.println(F("# Auto bit rate on")); break;
    case 'l': normalMode = false; applySpeed(); break;
    case 'n': normalMode = true; applySpeed(); break;
    case 'p': probeSpeeds(); break;
    case 's': printSummary(); break;
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
