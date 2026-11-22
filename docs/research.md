# Research notes and sources

What is known, what is assumed, and where it comes from. Please keep the distinction when adding to this file: mark anything not verified on real P3 hardware.

## Verified on P3 hardware (this project)

- Pinout of cluster 31453090, see [pinout.md](pinout.md)
- Two CAN buses, both with TLE6251DS high-speed transceivers, see [hardware.md](hardware.md)

## From other sources, not verified on P3

| Claim | Source | Note |
|---|---|---|
| Body/comfort network (LS-CAN, 125 kbit/s) carries the instrument cluster | general Volvo network descriptions | Bit rate to be confirmed with the sniffer |
| 2014+ P3 clusters add two wires in positions 7 and 8 of the white DIM connector for high-speed CAN; older ones have four wires | [Swedespeed TFT retrofit thread](https://www.swedespeed.com/threads/2014-tft-dim-retrofit-into-2011-2013-s60.516273/page-106), [d5t5 P3 TFT retrofit](https://d5t5.com/article/tft-retrofit) | Volvo's own pin numbers, which may not match this project's A/B naming |
| P3 TFT retrofit wiring: DIM pin 7 to OBD pin 6, DIM pin 8 to OBD pin 14 | [d5t5 P3 TFT retrofit](https://d5t5.com/article/tft-retrofit) | OBD 6/14 is the high-speed CAN pair |
| Replacement clusters need configuration in the car (VIDA/DiCE, P3Tool) for mileage, immobiliser and features | [NordCarolina P3Tool guide](https://nordcarolina.com/blog/2023/1/3/project-nashville-pt-4-tft-display) | Affects in-car use, probably not bench use |

## Bench observations, cluster 31453090 (2026-11-22)

- +12 V and ground only: gauge motors home and the display flickers faintly, then stays dark.
- Bus 1 (U6): ACKs test frames at 500 kbit/s cleanly (20/20, TEC 0, REC 0). At 125 and 250 the probe also counts "ACKs", but the receive error counter climbs (REC 19 and 14), so those are error frames from a 500 kbit/s node overlapping the all-zero test frame, not real ACKs. The 1000 kbit/s control rate gets no ACK at all. Bus 1 is therefore 500 kbit/s.
- The cluster kept ACKing for the whole 150 s test while being probed every 10 s.
- Bus 1: no frames transmitted by the cluster at 500 or 125 kbit/s, in ACK mode, over two power cycles each.
- Bus 2 (U7): no ACK at any rate in a first try, but the wiring was not verified at that time.

## Other leads

| Lead | Note |
|---|---|
| [YouTube: "Volvo p3 starting dash board on bench"](https://www.youtube.com/watch?v=zkJxJmyJBMs) (2021) | Shows a P3 cluster running on a bench. No description, no code; commenters asking for it got no public answer. |
| [Snap-on: XC60 medium-speed CAN](https://snapon.com/EN/US/Diagnostics/News-Center/Volvo-XC60-Medium-Speed-CAN-Bus-Failure) | Medium-speed bus at 125 kbit/s on OBD 3/11, high-speed at 500 kbit/s on OBD 6/14 |
| [opendbc volvo_v60_2015_pt.dbc](https://github.com/commaai/opendbc/blob/master/opendbc/dbc/volvo_v60_2015_pt.dbc) | P3 high-speed bus, 11-bit IDs, e.g. 0x148 vehicle speed (0.01 km/h). No DIM node. |
| [opendbc volvo_v40_2017_pt.dbc](https://github.com/commaai/opendbc/blob/master/opendbc/dbc/volvo_v40_2017_pt.dbc) | V40 (not P3): DIM sends 0x1D0 and 0x390, signals undecoded |
| Swedespeed threads [620291](https://www.swedespeed.com/threads/powering-up-the-dim-outside-of-the-car.620291/), [613841](https://www.swedespeed.com/threads/low-speed-can-w-the-dim.613841/) | P2 only. A bench P2 DIM sends two IDs for a few seconds after power-on, then stops when no other modules answer. |

## P2 material (do not assume it applies)

[andrewgabler/VolvoDIM](https://github.com/andrewgabler/VolvoDIM) drives a cluster at 125 kbit/s with 29-bit IDs such as `0x217FFC` (speed), `0x2803008` (RPM, backlight), `0x3C01428` (temperatures), `0x381526C` (time, fuel), and a wake/test frame on `0x000FFFFE`. It was tested on a 2005 S80 and a 2007 S60R cluster, which are **P2** platform. It is a useful reference for how a Volvo cluster can be simulated, but every ID must be checked against real P3 traffic.

## Related platforms

P3 shares much of its electrical architecture with Ford's EUCD cars (Mondeo Mk4, S-Max, Galaxy). Bench projects for those clusters may give hints. No public message map for them was found so far.
