# Connector pinout

## Cluster 31453090 (analog gauges, centre LCD, built 2017-06-14)

Label on the housing: `(P) 31453090`, `S-P 36003010`, `599-440U BK9KB`, `HL P/No EEA-BGA`.

The cluster has one white 10-pin connector, marked **CN1** on the board. Pin names below are this project's own naming, not Volvo's: look into the connector from the outside with the latch (`CN1`) at the top.

```
+-----------[ CN1 ]------------+
|  A1  A2  ..  ..  ..  ..  A7  |
|  B1  B2  B3  B4  B5  B6  B7  |
+------------------------------+
(.. = empty position)
```

| Pin | Function | Board connection |
|---|---|---|
| A1 | Ground | U6 pin 2 (GND) |
| A2 | CAN-L, bus 2 | U7 pin 6 |
| A7 | not identified | |
| B1 | not identified | |
| B2 | not identified | |
| B3 | CAN-H, bus 2 | U7 pin 7 |
| B4 | CAN-H, bus 1 | U6 pin 7 |
| B5 | CAN-L, bus 1 | U6 pin 6 |
| B6 | not identified | |
| B7 | +12 V | D2 (reverse-polarity diode) |

Which bus is low speed (125 kbit/s) and which is high speed (500 kbit/s) is not confirmed yet.

Forum reports say older P3 clusters (up to model year 2013) use only four wires in this connector, and that 2014 and later clusters add two more for a high-speed CAN bus. That is consistent with two CAN pairs on this unit, but it is not verified here.

## How it was measured

Power off, multimeter in continuity (beep) mode, with the cluster board removed from its housing:

1. One probe on a transceiver pin (see [hardware.md](hardware.md) for the chips), the other probe on each connector pin in turn.
2. A beep means a direct connection.
3. On an 8-pin transceiver, pin 1 is next to the dot; count down that side 1 to 4, then up the other side 5 to 8. For the TLE6251DS: pin 2 = GND, pin 6 = CAN-L, pin 7 = CAN-H.

Before first power-up, check that +12 V and ground are not shorted (B7 to A1 must not beep continuously).

An illustrated version of this procedure, in Latvian, is in [lv/kontaktu-atrasana.html](lv/kontaktu-atrasana.html).

## Other part numbers

Not measured yet. If you measure another cluster, please add a section here with the part number from the label and the same table.
