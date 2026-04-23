# UsbLinkStabilityTester
Deterministic USB/serial diagnostics for reconstructing and validating packet streams under real-world I/O conditions.

## Project Overview
`UsbLinkStabilityTester` is a C++20 layered system, not just a serial API wrapper.

Current state:
- `v0.1`: transport layer (serial I/O, status mapping, partial read/write handling)
- `v0.2`: protocol layer (packet framing, CRC validation, stream parsing, resync)

## Why This Exists
Serial data is a continuous byte stream. It does not include packet boundaries.

Real links also produce:
- partial reads,
- corrupted bytes,
- alignment loss,
- random noise between valid data.

The project is built to handle these conditions predictably.

## How It Works
Runtime loop:

```text
read -> append to buffer -> tryParsePacket() -> repeat
```

Parser behavior (`tryParsePacket`):
- scans for packet start using `magic`,
- waits for full header,
- waits for full payload,
- validates CRC before accept,
- drops one byte on CRC failure and retries,
- returns at most one packet per call.

## Architecture
```text
Application / Diagnostics
          |
          v
Protocol Layer (packet encoding, stream parsing, CRC validation, resync)
          |
          v
Transport Layer (ISerialTransport, POSIX backend)
          |
          v
OS driver + USB/serial device
```

## Usage Example
```cpp
#include <cstdint>
#include <span>
#include <vector>

#include "usblink/protocol/PacketEncoder.hpp"

void onIncomingBytes(std::span<const uint8_t> incoming)
{
    static std::vector<uint8_t> rxBuffer;

    rxBuffer.insert(rxBuffer.end(), incoming.begin(), incoming.end());

    usblink::protocol::PacketHeader header{};
    std::vector<uint8_t> payload;

    while (usblink::protocol::tryParsePacket(rxBuffer, header, payload))
    {
        // One validated packet is ready.
        // Use header.sequence / header.timestamp / payload.
    }
}
```

## Testing
Tests use Catch2. The parser is tested against real stream behavior, not ideal inputs.

They simulate real stream scenarios, including:
- partial header/payload arrival,
- CRC failures,
- garbage/noise bytes,
- misalignment and resynchronization,
- back-to-back packets,
- magic pattern inside payload.

## Documentation

Detailed design documents:

- [Protocol Layer](docs/protocol-layer.md)
- [Serial Frame](docs/serial_frame.md)
- [Transport Design](docs/transport_design.md)

## Build
```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
```

## Run Tests
```bash
ctest --test-dir build --output-on-failure
```

## Roadmap
- `v0.3`: protocol/transport metrics and long-run diagnostics
- `v0.4`: fault-injection and soak testing scenarios
- `v0.5`: additional transport backends
