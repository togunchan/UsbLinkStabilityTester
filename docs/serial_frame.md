# UART Serial Frame Structure (as used over USB Virtual COM Ports)

## Overview

UART (Universal Asynchronous Receiver/Transmitter) sends data over a serial line without a separate clock signal.  
Because transmitter and receiver are asynchronous, each transmitted value is wrapped in a frame so the receiver can detect boundaries and sample bits at the correct time.

Frames are used to:

- ensure reliable communication over asynchronous links,
- mark where each transfer starts and ends,
- define how many payload bits are present,
- optionally provide basic error detection.

## Full Frame Structure

```text
Idle line level: HIGH (1)
Time ------------------------------------------------------------->

+-----------+----+----+----+----+----+----+----+----+------------+--------+--------+
| Start Bit | D0 | D1 | D2 | D3 | D4 | D5 | D6 | D7 | Parity Bit | Stop 1 | Stop 2 |
+-----------+----+----+----+----+----+----+----+----+------------+--------+--------+
|     0     | b0 | b1 | b2 | b3 | b4 | b5 | b6 | b7 |   P (opt)  |   1    | 1 (opt)|
+-----------+----+----+----+----+----+----+----+----+------------+--------+--------+
```

## Start Bit

The start bit is always logic `0`. It creates a transition from idle (`1`) to active (`0`), which synchronizes receiver timing for the current frame.

```text
Time ----------------------------->
... 1 1 1 1 1 1 1 1 0 D0 D1 D2 ...
                    ^
                 Start Bit
```

## DataBits

`DataBits` is the payload width of the frame.

- Typical UART configurations: `5`, `6`, `7`, or `8` bits.
- `8` bits is most common for byte-oriented systems.
- UART transmits payload bits **LSB first**.

### DataBits breakdown (`D0-D7`, LSB first)

```text
Logical byte view (MSB -> LSB):
  b7  b6  b5  b4  b3  b2  b1  b0

UART wire order over time (LSB first):
Time --------------------------------->
  D0  D1  D2  D3  D4  D5  D6  D7
  b0  b1  b2  b3  b4  b5  b6  b7
```

## Parity

Parity is an optional single-bit check added after data bits.

- `None (N)`: parity bit is not transmitted.
- `Even (E)`: parity bit makes total count of `1` bits (data + parity) even.
- `Odd (O)`: parity bit makes total count of `1` bits (data + parity) odd.

### Parity calculation example

```text
Example data byte: 0x53 = 0b01010011

UART data order (D0..D7, LSB first):
1 1 0 0 1 0 1 0

Count of ones in data = 4

Even parity:
  P = 0  -> total ones = 4 (even)

Odd parity:
  P = 1  -> total ones = 5 (odd)
```

## StopBits

Stop bits are logic `1` bits at the end of a frame.

- They indicate frame completion.
- They provide timing margin before the next start bit.
- Common values are `1` or `2` stop bits (`1.5` exists on some UARTs).

```text
Time -------------------------->
... D7  [Parity]  Stop1  Stop2
      (optional)    1      1
```

## Common Configuration: `8N1`

`8N1` means:

- `8`: 8 data bits
- `N`: no parity
- `1`: 1 stop bit

This is widely used because it is simple, interoperable, and efficient for byte transfers.

### Example `8N1` frame for `0x53`

```text
Data byte: 0x53 = 0b01010011
D0..D7 (LSB first): 1 1 0 0 1 0 1 0

Time ----------------------------------------------------------->

+-------+----+----+----+----+----+----+----+----+------+
| Start | D0 | D1 | D2 | D3 | D4 | D5 | D6 | D7 | Stop |
+-------+----+----+----+----+----+----+----+----+------+
|   0   | 1  | 1  | 0  | 0  | 1  | 0  | 1  | 0  |  1   |
+-------+----+----+----+----+----+----+----+----+------+
```
