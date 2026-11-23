# Bench findings: cluster 31453090 (2017)

State as of 23 November 2026. Logs referenced here are in `logs/raw/`.

## What works

- **Power:** +12 V on A7, ground on A1. Gauge motors home at power-up, display flashes faintly once.
- **Bus 1 (U6) = 500 kbit/s high-speed CAN.** The cluster acknowledges frames but transmits nothing on its own.
- **Diagnostics (UDS over ISO-TP):** request `0x720`, response `0x728`. Default (`10 01`) and extended (`10 03`) sessions work. Serial F18C `000000378589`, F1A1 `31499578 AA`.
- **About 445 readable DIDs** (`22 xx xx`), see `31453090_did-scan_*.txt`. VIN (F190) gives NRC 0x31, also in the extended session.

## Configuration

- **0x400** on the car bus is the multiplexed vehicle configuration: byte 0 = index `0x01..0x24`, bytes 1-7 = parameters.
- **DID F106** (258 bytes) is the cluster's stored copy. Index `n` byte `j` of 0x400 maps to F106 payload offset `(n-1)*7 + j + 2`.
- **DID E103** lists the configuration items the cluster rejected: count byte, then pairs of (item, received value), with item = `(n-1)*7 + byte`.
- The simulator's 0x400 rows were matched to F106, including index 0x24, which the 2009 car does not send. With that, **E103 is empty** and **U2100-00 (configuration) is no longer active**.

## Still failing

- **U0001-55** (high-speed CAN, "not configured") becomes active immediately after a DTC clear.
- **U0100, U0140 and U0159** (lost communication) always stay at status 0x50, "test not completed". The cluster never runs its communication monitors, so it does not consider the vehicle to be on.
- Display dark, needles do not respond to RPM on 0x12A.
- **Bus 2 (U7)** never acknowledges at 125 or 500 kbit/s. This held with bus 1 fed, and after about 23,000 brute-force frames (`waker`).

## Tried without effect

| Experiment | Result |
|---|---|
| Sweep of 0x030 bytes 0-1 (ignition / usage mode in the 2009 car) | no change in DTC test status |
| 21 IDs that are in the 2015 V60 DBC but not in the 2009 log, sent with zero data | U3000-45 disappears, nothing else changes |
| Dropping the donor VIN (0x405 indices 0x10-0x12) | no lasting change |
| DID diff with simulation off, on and at 3000 rpm | nothing clearly tracks RPM |

## Odometer safety

The cluster holds 207,501 km (`03 2A 8D` in DIDs 61BB, D935, DD01). The donor car broadcasts its own distance in 0x405 index 02, 0x3EE and 0x3C0. The snapshot now sends the cluster's own value, so an accepted configuration cannot pull the odometer upward.

## Next steps

1. **Capture from a 2014-2018 P3 car** (S60, V60, XC60, V70, XC70, S80), at OBD pins 6/14 with the `car` build: 5 minutes, ignition II and engine running. The missing "vehicle on / network active" frame is expected to be there. No public log of such a car was found (GitHub, Zenodo, opendbc, d5t5 forum).
2. **DiCE + VIDA or P3Tool**, to read what the cluster expects and reconfigure it.
3. **Pass/fail test for each experiment:** clear DTCs (`14 FF FF FF`) and watch U0100. A status other than 0x50 means the cluster has woken its communication monitors.

## Privacy

The car log `XC70-2009_obd-6-14_500k_session1.txt` and the simulator snapshot had the donor car VIN in 0x405 indices 0x10-0x12. Those bytes are replaced with `X` (0x58).

## Software leads (research, 23 November 2026)

- **d5t5 VDASH TFT retrofit** (https://d5t5.com/article/tft-retrofit): a 2014-2017 DIM runs in a 2007-2013 P3 car with only HS-CAN added (DIM pins 7/8 to OBD 6/14), no gateway. VDASH first decodes the car's CEM (about 12 h), then backs up and reconfigures the DIM. So the old car's frames are likely enough, and the blocker is a DIM-side configuration/pairing step (U0001-55, "-55" = not configured), not a missing frame. VDASH needs a J2534/DiCE interface; whether its wizard runs without a car is unknown.
- **VIDA database extractors**: https://github.com/spnda/volvo_vida_db, https://github.com/Tigo2000/Volvo-VIDA. These are the only route found to the P3 car-config parameter list and DIM service definitions. They need your own copy of the VIDA DB.
- Not found: SimHub or game integrations, P3 bench testers. Minitools SEP-CAN-VOL covers P2 only.

## From VIDA 2014D (local database, not in this repo)

Closest DIM variant is **31412815 AD** (HS-CAN 500 kbit/s, request 0x720 / response 0x728). 451 of its 459 identifiers match the DIDs found on our 31453090 by scanning, so the layout is effectively the same.

| ID | Name in VIDA | Our reading |
|---|---|---|
| Routine 0x0202 | On-Demand Self-Test, "Gauge and lamp, quick test" (31 01 / 02 / 03) | not tried yet |
| D10E | HSCAN Network Management State (5 latest) | 20 21 20 = NMNormal, NMPreSleepNormal, NMNormal |
| D10F | MSCAN Network Management State | 07 01 FF |
| DD07 | Power mode: bits 7-5 quality, bits 4-0 extension | 0x67 = quality OK, 0x07 "combustion engine running" |
| DD01 | Total Distance | |
| 61BB | Odometer Store | 03 2A 8D = 207,501 km |
| E103 | Car Configuration Parameter Faults | |
| F106 | Vehicle Configuration Parameters | |
| EE49 / EE4A | Engine speed / vehicle speed needle angle | |
| EE60 / EE61 | Power management status, main node / private sub node | 00 / 00 |
| EE07 | Short/open LED of display backlight detection | |
| EECD | LCD backlight illumination dimming | |

The DIM therefore sees power mode "engine running" and HS network management "normal" from the simulator. The dark display is not caused by power mode.

VIDA has security code type `DIM000` "DIM, Y2, Default Key" for this variant (value kept local).

### More from VIDA (variant 31412815 AD)

- **Quick test, VIDA's own sequence:** `10 03`, `31 01 02 02`, wait about 20 s, `31 03 02 02`, `10 01`. Stop is `31 02 02 02`. No security access. It drives lamps, LCD and needles without any CAN signals, so it separates hardware faults from signal/config faults.
- **Speed signals not decoded:** D940 (engine speed) and D990 (vehicle speed) stay at 0, as do the needle angles EE49 / EE4A / EE4C, with simulation off, on and at 3000 rpm. Use D940 as feedback while trying RPM frame formats.
- **HS network management flapping:** D10E = 20 21 20 21 20, which alternates between NMNormal and NMPreSleepNormal. This probably causes U0001-55 and the lost-communication monitors never starting.
- **Backlight / dimming DIDs read zero:** EECD-EECF, EEF2, EEF4, EE80, EE81, and the LCD temperature A/D EE25.
- **Needle calibration looks blank:** EE7A-EE7E = FF. Routine 0xF002 "Perform Zero Point Detection" exists but no VIDA script uses it. Risky; only after the quick test.
- **F106 decode:** the stored configuration is an XC70 2009 D5244T4, left-hand drive, automatic, Europe.
- **No demo, dyno or showroom mode** for the DIM. No script writes F106. Security access is only used for theme/persona writes. The database has no CAN signal definitions.
- **Risky routines:** F003 odometer reset (high), F001 service reminder reset, F002 zero point detection, programming routines (never).

## Quick test result (23 November 2026)

`31 01 02 02` started (status 2), and `31 03 02 02` reported completed (status 0, no on-demand DTCs). **Needles swept, warning LEDs lit, buzzer sounded, display stayed dark.** So the gauges, LEDs and sound work, and the display fails even in self-test, without any CAN signals. EE25 (LCD temperature A/D) reads 0000 while the other analog inputs EE21-EE24 read normal values (01F1, 0248, 01F1, 020B). This points to the LCD module or its flex connection, not to the CAN simulation.
