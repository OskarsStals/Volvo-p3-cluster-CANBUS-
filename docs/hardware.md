# Hardware notes

Observed on cluster 31453090 (board marked `7GAM21143E0`, `MO723`, `CPCp G3 94V-0`).

## Main parts

| Ref | Part | Role |
|---|---|---|
| U5 | NXP/Freescale **MC9S12XHZ512VAG** | Microcontroller. The S12XHZ family is made for instrument clusters: CAN controllers, stepper motor drivers for the gauges and an LCD driver are built in. |
| U6 | Infineon **TLE6251DS** | High-speed CAN transceiver (ISO 11898-2), bus 1 |
| U7 | Infineon **TLE6251DS** | High-speed CAN transceiver (ISO 11898-2), bus 2 |
| U9 | ROHM **BD4275FP2** | 5 V regulator |
| U2 | 4266-2 (marking `4266-2 33`) | 3.3 V regulator |
| D2 | diode near the connector | Reverse-polarity protection on the +12 V input |
| ZDA1 to ZDA4 | | Protection diodes on the CAN lines |
| QA1, QA3, QA6 to QA9 | TI ULQ2003AT | Darlington drivers for warning lamps |
| RLY1 | not fitted | Empty relay footprint |
| SP1 | speaker | Chimes |

## What this means for bench use

- **Both CAN buses use standard high-speed transceivers**, not low-speed fault-tolerant ones (such as TJA1054/TJA1055). A normal MCP2515 shield with an MCP2551/TJA1050 style transceiver is electrically compatible with both buses.
- TLE6251DS pinout (DSO-8): 1 TxD, 2 GND, 3 VCC, 4 RxD, 5 SPLIT, 6 CANL, 7 CANH, 8 STB. Source: Infineon TLE6251DS datasheet.
- The transceivers have a standby mode. Together with the absence of an ignition wire this suggests the cluster wakes on CAN activity. Not yet confirmed.

## Disassembly

The black rear cover comes off first. The board is then held by screws; the components sit on the front side, under the dial. The gauge needles do not need to be removed to reach the transceivers and the connector solder joints.
