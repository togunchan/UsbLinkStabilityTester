# UsbLinkStabilityTester

`UsbLinkStabilityTester` is a C++20 USB/serial diagnostics project focused on deterministic reconstruction and validation of packet streams over unreliable byte-oriented transports.

The current `v0.2.0` focus is the protocol layer: framed packet encoding, CRC validation, stateful stream parsing, sequence tracking, and observable recovery from corrupted or fragmented input.

## Current Scope

- Deterministic packet framing over raw USB/serial byte streams.
- Stateful parser with explicit recovery states.
- RingBuffer-backed stream ingestion.
- Fragmentation-aware parsing across arbitrary read boundaries.
- CRC32 validation before packet acceptance.
- One-byte sliding resynchronization after corruption.
- Partial magic preservation across fragmented reads.
- Payload-size rejection for malformed headers.
- Protocol-level counters for valid traffic, invalid traffic, CRC failures, malformed headers, resynchronization, discarded bytes, and payload throughput.
- Sequence tracking for loss, duplicate, and reordered packet detection.

The project is intentionally validation-oriented. The parser is tested against stream behavior that serial systems actually produce: partial frames, noise before valid packets, corrupted headers, CRC mismatches, back-to-back packets, and magic-byte patterns inside payloads.

## Architecture

```text
Application / Diagnostics
          |
          v
Protocol Layer
  - packet encoding
  - stream parser state machine
  - CRC validation
  - recovery accounting
  - sequence tracking
          |
          v
Core Utilities
  - RingBuffer stream storage
          |
          v
Transport Layer
  - ISerialTransport
  - POSIX serial backend
          |
          v
OS driver + USB/serial device
```

Transport code moves bytes. Protocol code decides whether those bytes form a valid packet. Diagnostics code can then use parser and sequence metrics to characterize link behavior.

## Protocol Frame

Each packet is serialized as:

```text
[PacketHeader][payload]
```

`PacketHeader` is packed to keep the wire layout deterministic:

```text
Offset  Size  Field
0       4     magic
4       4     sequence
8       8     timestamp
16      4     payloadSize
20      4     crc
```

The CRC is calculated over the packed header with `crc` zeroed, followed by the payload. The parser accepts a packet only after the computed CRC matches the received CRC.

Current protocol constraints:

- `MAGIC`: `0xAABBCCDD`
- `MAX_PAYLOAD_SIZE`: `4096` bytes
- Wire byte order follows the native layout of the current implementation.

## Parser State Machine

The parser operates on a rolling `RingBuffer` and explicit `ParseState`:

```text
SeekMagic -> WaitHeader -> WaitPayload -> Validate
     ^                                      |
     |                                      |
     +----------- recovery / accept --------+
```

State responsibilities:

- `SeekMagic`: scan the stream for the packet magic value. If no full magic exists, discard bytes while preserving the last three bytes so a fragmented magic sequence can complete on a later read.
- `WaitHeader`: wait until a full `PacketHeader` is buffered, then copy it into parser working state.
- `WaitPayload`: wait until the entire payload declared by `payloadSize` is buffered.
- `Validate`: compute CRC over the candidate frame. On success, emit one validated packet and consume the full frame. On failure, consume one byte and return to `SeekMagic`.

`tryParsePacket()` returns at most one packet per call. Callers append bytes to the ring buffer, call the parser repeatedly, and retain parser state between calls.

```cpp
#include "usblink/core/RingBuffer.hpp"
#include "usblink/protocol/PacketEncoder.hpp"

#include <cstdint>
#include <span>
#include <vector>

void onIncomingBytes(std::span<const uint8_t> incoming)
{
    static usblink::core::RingBuffer rxBuffer{8192};
    static usblink::protocol::ParseState state{usblink::protocol::ParseState::SeekMagic};
    static usblink::protocol::PacketHeader workingHeader{};
    static usblink::protocol::ProtocolStats stats{};

    rxBuffer.write(incoming);

    usblink::protocol::PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;

    while (usblink::protocol::tryParsePacket(
        rxBuffer, state, workingHeader, parsedHeader, parsedPayload, stats))
    {
        // parsedHeader and parsedPayload describe one CRC-validated packet.
    }
}
```

## Recovery Semantics

The parser is self-recovering by design:

- Garbage before a valid magic value is discarded and counted.
- CRC failure consumes one byte, then resumes scanning.
- Oversized `payloadSize` is treated as a malformed header and resynchronized.
- When no complete magic is found, the parser preserves the final `sizeof(uint32_t) - 1` bytes to avoid discarding a magic sequence split across reads.
- Payload bytes are not scanned for magic while a candidate frame is incomplete or awaiting CRC validation.

This recovery policy is conservative. It favors deterministic alignment recovery over aggressive frame skipping, which reduces the chance of stepping over the next valid packet after corruption.

## Deterministic Parsing Philosophy

The protocol layer treats serial input as an arbitrary byte stream, not as message-delimited reads. A valid packet is emitted only when all of the following are true:

- a magic-aligned candidate exists,
- the full packed header is available,
- `payloadSize` is within the configured bound,
- the full declared payload is available,
- the CRC validates against the exact candidate frame.

All rejection paths have explicit consumption behavior and update observability counters. This makes parser behavior reproducible under tests and useful for long-running diagnostics.

## Observability Metrics

`ProtocolStats` is updated by the parser:

```text
parsedPackets       accepted packets emitted by the parser
validPackets        CRC-valid packets
invalidPackets      rejected candidate frames
crcFailures         candidate frames rejected by CRC
malformedHeaders    headers rejected before payload validation
resyncEvents        parser realignment events
discardedBytes      bytes consumed during recovery
bytesReceived       caller-owned ingress counter
totalPayloadBytes   accepted payload bytes
```

`bytesReceived` is provided for higher-level integration; the parser does not own transport reads and therefore does not increment it internally.

## Sequence Tracking

`SequenceTracker` observes validated packet sequence numbers after parsing. It maintains:

```text
receivedPackets                 valid in-order or forward-progress packets
lostPackets                     gaps between expected and observed sequence
duplicateOrReorderedPackets     sequence values older than expected
expectedSequence                next sequence number expected
```

The first observed packet initializes the tracker. Forward gaps increase `lostPackets`; older sequence values are counted as duplicates or reordering and do not advance the expected sequence. `uint32_t` wraparound is handled by normal unsigned arithmetic for the next expected value.

## Validation

Catch2 coverage currently exercises:

- complete valid packets,
- partial headers,
- partial payloads,
- byte-by-byte and chunked stream arrival,
- back-to-back packets,
- zero-length payloads,
- payloads at `MAX_PAYLOAD_SIZE`,
- large payload streams,
- garbage before valid packets,
- deterministic random noise between packets,
- CRC failure and recovery,
- multiple consecutive CRC failures,
- corrupted header fields,
- malformed oversized payload headers,
- partial magic preservation,
- magic bytes embedded inside payloads,
- parser metrics accounting,
- sequence tracking for normal flow, gaps, duplicates, reordering, and wraparound.

## Build

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
```

## Run Tests

```bash
ctest --test-dir build --output-on-failure
```

## Documentation

- [Protocol Layer](docs/protocol-layer.md)
- [Serial Frame](docs/serial_frame.md)
- [Transport Design](docs/transport_design.md)

## Roadmap

- Expand protocol and transport metrics into long-run diagnostics reports.
- Add fault-injection and soak-test scenarios.
- Add additional transport backends behind `ISerialTransport`.
