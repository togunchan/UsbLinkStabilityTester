# Changelog

All notable changes to `UsbLinkStabilityTester` are documented here.

## v0.2.0 - Protocol Layer Maturity

`v0.2.0` moves the project from transport scaffolding into deterministic protocol validation. The main engineering goal is to make packet reconstruction observable, reproducible, and resilient to realistic USB/serial stream behavior: fragmentation, corruption, noise, and alignment loss.

### Protocol Framing

- Added packed `PacketHeader` framing with magic, sequence, timestamp, payload size, and CRC fields.
- Added deterministic packet encoding for `[header][payload]` frames.
- Added CRC32 validation over the header with the CRC field zeroed plus the payload.
- Added `MAX_PAYLOAD_SIZE` enforcement to reject malformed headers before trusting declared frame length.

Why it matters: raw serial reads do not preserve message boundaries. Framing and CRC validation establish the minimum contract required before higher-level diagnostics can trust a packet.

### Stream Parser

- Added RingBuffer-backed parsing for continuous byte streams.
- Added explicit parser states: `SeekMagic`, `WaitHeader`, `WaitPayload`, and `Validate`.
- Added support for fragmented headers and payloads across arbitrary read boundaries.
- Added one-packet-per-call parser behavior for deterministic caller control.
- Added correct handling for back-to-back packets and magic-byte patterns embedded inside payload data.

Why it matters: the parser now models the transport as a stream instead of assuming ideal packet-sized reads.

### Recovery Behavior

- Added self-recovery from garbage before valid packets.
- Added CRC-failure recovery using a one-byte sliding resynchronization policy.
- Added malformed-header recovery for oversized payload declarations.
- Added preservation of partial magic suffixes when a read ends mid-marker.
- Added deterministic recovery after multiple consecutive corrupt packets and noise between valid packets.

Why it matters: recovery behavior is now explicit and testable. The parser consumes known byte counts on each rejection path instead of relying on ambiguous frame skipping.

### Observability

- Added `ProtocolStats` integration for parser-level accounting.
- Added counters for parsed packets, valid packets, invalid packets, CRC failures, malformed headers, resynchronization events, discarded bytes, and accepted payload bytes.
- Reserved `bytesReceived` for higher-level transport integration.

Why it matters: diagnostics need failure modes, not just pass/fail packet output. Parser counters expose link behavior and recovery cost.

### Sequence Tracking

- Added `SequenceTracker` for validated packet streams.
- Added accounting for received packets, sequence gaps, duplicates, and reordered packets.
- Added expected-sequence tracking with unsigned wraparound behavior.

Why it matters: CRC confirms frame integrity, but sequence tracking identifies delivery-level behavior such as loss, duplication, and reordering.

### Validation

- Expanded Catch2 coverage for valid frames, fragmentation, back-to-back packets, CRC failures, malformed headers, garbage/noise recovery, partial magic preservation, boundary payload sizes, and parser metrics.
- Added deterministic tests for sequence gaps, duplicates, reordering, accumulated mixed traffic, and `uint32_t` wraparound.

Why it matters: protocol behavior is now specified by tests that exercise stream conditions rather than only ideal inputs.

### Documentation

- Updated release documentation to describe the protocol architecture, parser state machine, recovery semantics, observability metrics, and sequence tracking.
- Added this changelog to summarize the architectural intent behind `v0.2.0`.

## v0.1.0 - Transport Foundation

- Established the C++20 CMake project structure.
- Added the `ISerialTransport` abstraction for byte-stream serial communication.
- Added the POSIX serial transport implementation.
- Added basic transport-oriented documentation and project scaffolding.
