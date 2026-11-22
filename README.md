# Volvo P3 instrument cluster on the bench

Run a Volvo **P3 platform** instrument cluster (DIM, Driver Information Module) outside the car with an **Arduino Uno and an MCP2515 CAN shield**, and document its CAN messages so others can drive it too: gauges for sim racing, bench repair, retrofit work, or just learning how a car network works.

P3 cars: S60 II, V60, XC60 (first generation), S80 II, V70 III, XC70 II (2007 to 2018).

> **Status:** early. The connector pinout of one cluster is measured and verified, and the hardware is identified. The sniffer firmware builds but has not yet been run against the cluster. Gauge control is not solved yet. Contributions of CAN logs are very welcome, see [CONTRIBUTING.md](CONTRIBUTING.md).

## Why another project?

Most existing Arduino projects for Volvo clusters (for example [VolvoDIM](https://github.com/andrewgabler/VolvoDIM)) were built and tested on **P2** clusters (2005 S80, 2007 S60R). P2 uses different electronics. Their CAN IDs, wake-up frames and pinouts should not be assumed to work on a P3 cluster. This repository starts from measurements on real P3 hardware.

## What you need

| Part | Notes |
|---|---|
| P3 instrument cluster | Tested unit: part no. **31453090**, analog gauges with centre LCD, built June 2017 |
| Arduino Uno R3 | Any Uno compatible board |
| MCP2515 CAN shield | Tested: SparkFun style clone with DB9, microSD, **16 MHz** crystal. Set the `PWR_OBD` switch to **OFF** |
| 12 V power supply | A small one (0.5 to 1 A) is ideal: it limits current if something is wired wrong |
| Multimeter | To verify the pinout on your own unit before powering it |

## Pinout (cluster 31453090)

Looking into the white connector **CN1** on the back of the cluster, latch at the top:

```
+-----------[ CN1 ]------------+
|  A1  A2  ..  ..  ..  ..  A7  |
|  B1  B2  B3  B4  B5  B6  B7  |
+------------------------------+

(.. = empty position)
```

| Pin | Function | Verified by |
|---|---|---|
| **A7** | +12 V supply | continuity to reverse-polarity diode D2, powered on the bench |
| **A1** | Ground | continuity to transceiver U6 pin 2 |
| **B4** | CAN-H, bus 1 | transceiver U6 pin 7 |
| **B5** | CAN-L, bus 1 | transceiver U6 pin 6 |
| **B3** | CAN-H, bus 2 | transceiver U7 pin 7 |
| **A2** | CAN-L, bus 2 | transceiver U7 pin 6 |

There is no separate ignition wire: the cluster is expected to wake on CAN traffic. An earlier version said B7 for +12 V; the A/B naming may be mirrored, so confirm +12 V by continuity to D2. Details and the measuring method are in [docs/pinout.md](docs/pinout.md). **Measure your own unit before applying power.** Other part numbers and model years may differ.

## Wiring

```
12 V supply (+) ---------------------- A7
12 V supply (-) ------+--------------- A1
                      |
Arduino GND ----------+     (common ground is required)
Shield CANH -------------------------- B4   (bus 1)
Shield CANL -------------------------- B5   (bus 1)
Arduino USB --- PC
```

Bus 1 is the 500 kbit/s high-speed bus. Bus 2 (B3 / A2) has not answered yet.

## Firmware

Built with [PlatformIO](https://platformio.org/). Each stage of the project is its own environment under `src/`.

| Environment | Folder | What it does |
|---|---|---|
| `sniffer` | `src/sniffer/` | Listens to the bus, auto-detects bit rate, probes whether the cluster acknowledges frames. Transmits nothing unless you ask it to. |
| `car` | `src/sniffer/` | **Car logging build.** Locked to 500 kbit/s listen-only: cannot transmit or ACK. Safe on a car's OBD port (pins 6/14). |
| `simulator` | `src/simulator/` | **Bench only, transmits.** Replays a 2009 XC70 high-speed snapshot, with the 0x400 configuration matched to the cluster's stored config, plus diagnostic commands (DTCs, DID scan, config rejects). |
| `waker` | `src/waker/` | **Bench only, transmits.** Brute-force search for a frame that wakes bus 2. |

```
pio run -e sniffer -t upload
pio device monitor
```

Serial commands of the sniffer (115200 baud):

| Key | Action |
|---|---|
| `1` / `2` / `5` | Bit rate 125 / 250 / 500 kbit/s |
| `a` | Auto-step bit rate while the bus is silent (default) |
| `l` | Listen-only: no transmit, no ACK |
| `n` | Normal mode: acknowledge frames, send no data |
| `p` | Bit-rate probe: send a harmless test frame (ID `0x7F0`) at each rate and count acknowledgements |
| `s` | Summary of all IDs seen |
| `h` | Help |

The Arduino IDE works too: copy `src/sniffer/main.cpp` into a sketch and install the **mcp_can** library by coryjfowler.

## Repository layout

```
src/<stage>/     firmware, one folder per PlatformIO environment
docs/            pinout, hardware notes, research and sources
docs/lv/         Latvian material (illustrated pin-finding guide)
logs/            CAN captures from real clusters and cars
dbc/             decoded message definitions, once known
```

## Status (November 2026)

The cluster answers diagnostics and accepts the simulated configuration, but the display stays dark and the needles do not move. Full notes, DTCs and next steps: [docs/findings.md](docs/findings.md).

## Roadmap

1. Confirm bit rate of both buses and whether the cluster transmits on its own
2. Find the frames that wake the cluster and keep it awake
3. Decode speed, RPM, fuel, temperature, indicators, backlight
4. Simulator firmware, and a DBC file for the decoded signals
5. Optional: SimHub integration for sim racing

## Safety

Work on the bench only. Only the `car` build may be connected to a car: it is listen-only. Never upload `simulator` or `waker` while the Arduino is connected to a car. Sending wrong frames on a vehicle bus can disturb safety systems.

## License

[MIT](LICENSE). Volvo is a trademark of its owner. This project is not affiliated with or endorsed by Volvo Cars.
