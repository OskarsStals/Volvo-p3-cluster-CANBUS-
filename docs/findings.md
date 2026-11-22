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
