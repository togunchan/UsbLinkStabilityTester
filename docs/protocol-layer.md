# Protocol Layer

## Overview

The protocol layer reconstructs validated packets from a raw serial byte stream. It is designed around deterministic behavior under fragmentation, corruption, and alignment loss.

Transport code provides bytes only. It does not know where packets start, whether a frame is complete, or whether payload bytes were corrupted. The protocol layer owns those decisions:

- identify packet boundaries with a fixed magic value,
- wait for complete headers and payloads across arbitrary read boundaries,
- reject malformed headers before trusting declared frame length,
- validate candidate frames with CRC32,
- recover by resynchronizing on the next plausible packet boundary,
- expose parser behavior through counters.

## Packet Format

Packets are serialized as:

```text
[HEADER][PAYLOAD]
```

`PacketHeader`:

```text
Offset  Size  Field
0       4     magic
4       4     sequence
8       8     timestamp
16      4     payloadSize
20      4     crc
```

Field meaning:

- `magic` (`0xAABBCCDD`): synchronization marker.
- `sequence`: packet order field used by `SequenceTracker`.
- `timestamp`: sender-side timing metadata.
- `payloadSize`: payload length in bytes.
- `crc`: CRC32 of header plus payload, with the CRC field zeroed during calculation.

The header is packed with `#pragma pack(push, 1)` to avoid compiler padding. Current wire byte order follows native layout.

## Encoding

`encodePacket()` builds a deterministic `[header][payload]` byte vector:

1. Copy the caller-provided header.
2. Set `payloadSize` from the payload span.
3. Set `crc` to zero.
4. Serialize the packed header with `std::bit_cast`.
5. Compute CRC32 over the zero-CRC header bytes and payload.
6. Write the computed CRC into the header.
7. Serialize the final header followed by the payload.

The CRC implementation is reflected CRC32:

- initial value: `0xFFFFFFFF`
- polynomial: `0xEDB88320`
- final value: bitwise inversion

The implementation is bitwise and explicit. It favors clarity and deterministic behavior over lookup-table throughput.

## Parser State Machine

`tryParsePacket()` parses from `core::RingBuffer` and carries explicit parser state between calls:

```text
SeekMagic -> WaitHeader -> WaitPayload -> Validate
     ^                                      |
     |                                      |
     +----------- recovery / accept --------+
```

Inputs and outputs:

- `RingBuffer& buffer`: rolling stream storage owned by the caller.
- `ParseState& state`: parser state retained across calls.
- `PacketHeader& hdr`: working header retained while waiting for payload.
- `PacketHeader& outHeader`: accepted packet header.
- `std::vector<uint8_t>& outPayload`: accepted packet payload.
- `ProtocolStats& stats`: parser accounting.

The parser returns at most one packet per call.

### `SeekMagic`

The parser scans for `MAGIC`.

If magic is found at a nonzero offset, bytes before it are consumed and counted as discarded. If no complete magic is found, the parser consumes all but the last three bytes. Preserving three bytes allows a magic value split across reads to complete later.

### `WaitHeader`

The parser waits until `sizeof(PacketHeader)` bytes are buffered. It then copies the candidate header out of the ring buffer.

If `payloadSize > MAX_PAYLOAD_SIZE`, the header is treated as malformed. The parser consumes one byte, records the malformed header and invalid packet, returns to `SeekMagic`, and continues recovery.

### `WaitPayload`

The parser waits until the complete declared frame is buffered:

```text
sizeof(PacketHeader) + payloadSize
```

No payload bytes are accepted or scanned as independent packets while the candidate frame is incomplete.

### `Validate`

The parser computes CRC over a temporary header with `crc = 0` and the candidate payload bytes from the ring buffer.

On success:

- copy header and payload to outputs,
- consume the full frame,
- reset state to `SeekMagic`,
- increment valid packet and payload counters,
- return `true`.

On failure:

- consume one byte,
- reset state to `SeekMagic`,
- increment CRC failure, invalid packet, and discarded byte counters,
- return `false`.

## Recovery Semantics

Recovery is intentionally conservative:

- garbage before magic is discarded only up to the next candidate magic,
- CRC failure advances by one byte,
- malformed oversized headers advance by one byte,
- failed magic scans preserve a partial suffix that could become a complete magic on a later read.

One-byte sliding recovery is slower in heavy noise, but it avoids skipping over a valid packet start after corruption. This is the intended tradeoff for diagnostics: predictable recovery semantics are more valuable than optimistic skipping.

## Observability

`ProtocolStats` records parser behavior:

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

`bytesReceived` is intentionally not incremented by the parser because the parser does not perform reads from the transport. Integration code can update it when bytes enter the ring buffer.

## Sequence Tracking

`SequenceTracker` is separate from parsing. It should observe sequence numbers only after CRC validation.

Behavior:

- first observed packet initializes `expectedSequence`,
- in-order packets advance `expectedSequence`,
- forward jumps count missing sequence values as lost packets,
- older sequence values count as duplicate or reordered packets,
- duplicate/reordered packets do not advance expected sequence,
- `uint32_t` wraparound is handled by unsigned arithmetic.

This separation keeps frame integrity (`tryParsePacket`) distinct from delivery behavior (`SequenceTracker`).

## Validation Coverage

Catch2 tests in `tests/protocol` cover:

- valid packet parse,
- partial header and partial payload arrival,
- stream-split and byte-chunked input,
- back-to-back packets,
- magic values inside payload data,
- zero-length payloads,
- large payloads and `MAX_PAYLOAD_SIZE`,
- oversized payload header rejection,
- CRC mismatch recovery,
- multiple consecutive CRC failures,
- corrupted header fields,
- deterministic random noise between valid packets,
- partial magic preservation,
- parser metrics accounting,
- sequence gaps, duplicates, reordering, mixed traffic, and wraparound.

These tests define parser behavior against stream conditions rather than ideal message boundaries.

## Known Constraints

- Payload bytes are copied into `outPayload` on acceptance.
- CRC validation reads candidate payload bytes from the ring buffer one byte at a time.
- Wire byte order is native-layout dependent.
- The parser requires caller-managed ring buffer capacity.
- `bytesReceived` is an integration counter, not parser-owned state.

Potential future work includes explicit wire endianness, zero-copy payload views, CRC over contiguous ring-buffer windows, richer diagnostics aggregation, and additional transport backends.
