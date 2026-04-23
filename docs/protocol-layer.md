# v0.2 Protocol Layer

## 1. Overview
This layer adds packet framing and integrity checks on top of raw serial transport.

Transport only provides raw bytes. It does not tell us where a packet starts or ends. It also cannot tell us if a packet was corrupted in the middle.

So the protocol layer does four things:
- defines a fixed binary header,
- stores payload size in that header,
- validates each packet with CRC32,
- recovers from noise by resynchronizing on magic bytes.

The implementation is stream-first. That is important because serial input arrives in arbitrary chunks.

## 2. Packet Format
On the wire, a packet is:

```text
[HEADER][PAYLOAD]
```

Header type is `PacketHeader`:

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
- `sequence`: packet order / tracking field.
- `timestamp`: sender-side timing metadata.
- `payloadSize`: payload length in bytes.
- `crc`: CRC32 of header+payload, with `crc` field zeroed during calculation.

`PacketHeader` is packed with `#pragma pack(push, 1)`. That avoids padding and keeps byte layout deterministic.

Why this format:
- fixed header makes parsing predictable,
- variable payload keeps it flexible,
- CRC protects against silent corruption.

## 3. Encoding Logic
`encodePacket()` builds a packet in two phases.

1. Copy the input header to a local `hdr`.
2. Set `hdr.payloadSize` from payload length.
3. Set `hdr.crc = 0`.
4. Build temporary bytes: `[hdr-with-zero-crc][payload]`.
5. Compute CRC32 over that temporary block.
6. Write computed CRC into `hdr.crc`.
7. Build final bytes: `[hdr-with-real-crc][payload]`.

The header is serialized by viewing it as raw bytes (`reinterpret_cast<const uint8_t*>`). This works because the struct is tightly packed and has no padding.

`crc` is zero during hashing so both encoder and parser hash exactly the same content. If CRC were included as-is, hashing would be self-referential.

Temporary buffers are used for clarity and deterministic behavior. The current CRC function expects a contiguous span.

## 4. CRC Logic
`computeCRC32()` implements reflected CRC32:
- initial value: `0xFFFFFFFF`
- polynomial: `0xEDB88320`
- final step: `~crc`

It is a bitwise implementation (8 inner iterations per input byte), without lookup tables.

Why this matters in practice:
- serial links can return bytes that look valid structurally but contain damage,
- CRC is the final gate before accepting a packet.
- Without CRC, a corrupted payload could still look like a valid packet.

This version favors simple, explicit code over maximum CRC throughput.

## 5. Stream Parsing Model
`tryParsePacket()` parses from a mutable `std::vector<uint8_t>& buffer`.

That buffer is a rolling stream window. Caller appends bytes from serial reads, then calls parser repeatedly.

Model rules:
- input is a continuous byte stream,
- parser may need multiple calls before one packet becomes complete,
- parser returns at most one packet per call,
- parser mutates buffer by erasing consumed or invalid bytes.

```text
read chunk -> append to buffer -> tryParsePacket() -> repeat
```

## 6. Packet Parsing Steps (IMPORTANT)
`tryParsePacket()` flow:

1. **Find candidate start**
   - Call `findMagicOffset(buffer, MAGIC)`.
   - If magic is not found, return `false`.

2. **Drop bytes before magic**
   - If magic is at offset `> 0`, erase leading garbage.

3. **Check header completeness**
   - If `buffer.size() < sizeof(PacketHeader)`, return `false`.

4. **Read header**
   - Copy first header bytes into local `PacketHeader hdr` using `memcpy`.

5. **Check full frame size**
   - Compute `totalSize = sizeof(PacketHeader) + hdr.payloadSize`.
   - If buffer does not contain `totalSize` bytes yet, return `false`.

6. **Prepare CRC verification data**
   - Copy payload candidate into `outPayload`.
   - Copy `hdr` to temp header and set temp `crc = 0`.
   - Build temporary byte block `[tempHeader][payload]`.

7. **Validate CRC**
   - Compute CRC32 on the temporary block.
   - Compare with `hdr.crc`.

8. **CRC failed**
   - Erase only one byte from buffer start.
   - Return `false`.

9. **CRC passed**
   - Write `outHeader = hdr`.
   - Erase full packet (`totalSize` bytes) from buffer.
   - Return `true`.

Short parser state view:

```text
seek magic -> wait header -> wait payload -> validate crc
```

## 7. Resynchronization Logic
When CRC check fails, parser drops one byte and tries again on next call.

Why one byte, not whole frame:
- after corruption, alignment is unknown,
- dropping too much can skip a real packet start,
- one-byte sliding eventually tests every alignment.
- This guarantees that the parser will eventually find a valid packet if one exists in the stream.

This is slower in heavy noise, but it is robust. If valid packets continue to arrive, parser can recover.

## 8. Edge Cases (from tests)
Catch2 tests in `tests/protocol/PacketParserTests.cpp` cover these cases:

- valid packet parse,
- partial header,
- partial payload,
- garbage before valid packet,
- CRC mismatch and resync,
- back-to-back packets (one packet per call),
- chunked / byte-by-byte stream input,
- magic pattern inside payload,
- zero-length payload,
- large payload (2048 bytes),
- multiple consecutive CRC failures,
- corrupted header fields,
- exact boundary sizes (`sizeof(header)` and exact `totalSize`),
- deterministic random noise between valid packets.

These tests verify correctness under real stream behavior, not ideal message boundaries.

## 9. Limitations
Current design is correct for v0.2, but there are known costs:

- front `vector.erase()` is O(n),
- CRC path builds temporary buffers,
- payload is copied into `outPayload`,
- wire byte order is based on native memory layout. This means packets are not portable across different endianness architectures,
- no strict built-in max payload bound,
- CRC is recomputed per parse attempt.

## 10. Possible Improvements
1. Use ring buffer or head index to avoid O(n) front erases.
2. Add zero-copy payload view path.
3. Compute CRC directly on existing buffer windows to reduce temporary allocations.
4. Define explicit wire endianness for cross-platform consistency.
5. Add hard payload size limits for early rejection.
6. Formalize parser states (`SeekMagic`, `WaitHeader`, `WaitPayload`, `Validate`).
7. Reserve version/flags fields for protocol evolution.
