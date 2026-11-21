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

## P2 material (do not assume it applies)

[andrewgabler/VolvoDIM](https://github.com/andrewgabler/VolvoDIM) drives a cluster at 125 kbit/s with 29-bit IDs such as `0x217FFC` (speed), `0x2803008` (RPM, backlight), `0x3C01428` (temperatures), `0x381526C` (time, fuel), and a wake/test frame on `0x000FFFFE`. It was tested on a 2005 S80 and a 2007 S60R cluster, which are **P2** platform. It is a useful reference for how a Volvo cluster can be simulated, but every ID must be checked against real P3 traffic.

## Related platforms

P3 shares much of its electrical architecture with Ford's EUCD cars (Mondeo Mk4, S-Max, Galaxy). Bench projects for those clusters may give hints. No public message map for them was found so far.
