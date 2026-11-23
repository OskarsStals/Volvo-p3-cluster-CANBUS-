// Per-ID send period in ms, from the XC70 2009 log (most common gap between
// frames of each ID; 0x400/0x405 are per multiplexed frame). Same order as SIM_IDS.
#pragma once
static const uint16_t SIM_PERIOD_MS[] PROGMEM = {120, 40, 30, 40, 40, 40, 40, 30, 120, 120, 40, 40, 40, 40, 120, 40, 40, 40, 40, 40, 40, 40, 120, 120, 120, 120, 40, 40, 25, 25, 360, 45, 50, 120, 120, 60, 200, 250, 250, 150, 180, 225, 400, 1350, 720};
