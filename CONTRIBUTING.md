# Contributing

The most valuable help right now is **real data from P3 clusters and cars**.

## CAN logs

Put logs in `logs/` and open a pull request. Please include:

- **File name:** `<part number>_<bus>_<bitrate>_<what happens>.txt`, for example `31453090_bus1_125k_power-on.txt` or `XC70-2012_obd-3-11_125k_ignition-on-idle.txt`
- **A short header at the top of the file:** cluster part number (from the label) or car model and year, which pins or OBD pins you tapped, bit rate, and what was happening (power on, ignition on, engine idling, indicator left, and so on)
- **Format:** the sniffer's own output is fine (`millis  STD|EXT id  dlc  data`). Candump and SavvyCAN CSV are fine too, say which one.

Logs from a running car are the fastest way to decode messages. Tap only in **listen-only** mode, and never transmit on a car's network.

## Pinouts of other clusters

Measure as described in [docs/pinout.md](docs/pinout.md) and add a section for your part number.

## Decoded signals

When a signal is understood (ID, byte, scaling, offset), add it to `docs/` and later to the DBC file in `dbc/`, with the log that proves it.

## Code

Keep each firmware stage in its own folder under `src/` with its own PlatformIO environment. Keep the sniffer safe by default: it must not transmit unless the user explicitly asks.
