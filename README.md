# UsbLinkStabilityTester

Deterministic USB/serial diagnostics that expose real POSIX I/O failure modes instead of abstracting them away.

**Current version:** `v0.1` (`Serial Transport Layer`)

## Project Overview

`UsbLinkStabilityTester` is a C++20 systems project that isolates serial transport behavior and makes it observable, deterministic, and testable. The current implementation targets POSIX systems and uses `open(2)`, `read(2)`, `write(2)`, and `termios` directly.

This project is intentionally transport-first. There is no UI layer in scope for `v0.1`.

## Why This Project Exists

USB CDC and UART-style links often fail in ways that are not obvious at application level: short writes, interrupted syscalls, transient device states, permission issues, and inconsistent driver behavior.

Most application code treats serial I/O as "write once, read once" and loses diagnostic value when the OS returns partial progress or low-level errors. This project enforces a transport contract that keeps those signals intact, so reliability work is driven by observable behavior rather than assumptions.

## v0.1 Scope (Serial Transport Layer)

Implemented in `v0.1`:

- `ISerialTransport` interface for backend-independent transport contracts
- `PosixSerialTransport` concrete backend
- `termios` configuration path (raw mode + baud rate)
- Loop-based `write()` with partial-write completion logic
- Loop-based `read()` with explicit status/byte-count return
- `errno`-driven mapping into transport-level status codes
- `std::span` for safe non-owning buffer views
- RAII cleanup via deterministic descriptor closure in destructor

Out of scope in `v0.1`:

- Framing/protocol parsing
- End-to-end message integrity checks
- Metrics/telemetry and long-run reporting
- Multi-backend support (Win32, libusb, etc.)

## Architecture

The transport layer is intentionally narrow and explicit:

```text
Application / Diagnostics Logic
            |
            v
     ISerialTransport
            |
            v
   PosixSerialTransport
            |
            v
 POSIX syscalls + termios
            |
            v
   Kernel TTY/USB-serial driver
            |
            v
     Target USB/serial device
```

Key property: higher layers depend on `ISerialTransport`, not POSIX details. This keeps protocol and diagnostics logic portable while still allowing low-level behavior to be surfaced precisely. Each layer is replaceable in isolation as long as it preserves the transport contract.

## Design Principles

This project treats abstraction as a control boundary, not a mechanism for hiding OS behavior.

- Preserve kernel-level outcomes (`EINTR`, `EAGAIN`, short I/O) as explicit transport-level states
- Return progress (`bytesRead`/`bytesWritten`) on failure paths to support diagnostics and recovery policies
- Prefer deterministic resource ownership and teardown over implicit lifetime assumptions

## Design Decisions

### Why `std::span`

`std::span` provides a non-owning view with explicit extent in one type. It removes pointer/length drift risks, avoids copies, and keeps the API compatible with `std::array`, `std::vector`, and fixed buffers.

### Why Partial Write Handling

On POSIX, `write()` may legally complete with fewer bytes than requested. Treating that as full success corrupts stream semantics and weakens diagnostics. `v0.1` drives `write()` to completion or terminal status and returns exact `bytesWritten` for post-failure analysis.

### Why `errno` Mapping

Raw `errno` values are backend-specific and leak OS coupling into higher layers. Mapping failures to `TransportStatus` provides a stable cross-layer contract (`PortNotFound`, `AccessDenied`, `Timeout`, `IoError`, etc.) while preserving actionable error classes at the transport boundary.

## Example

```cpp
#include <array>
#include <cstdint>

#include "usblink/transport/SerialConfig.hpp"
#include "usblink/transport/SerialTypes.hpp"
#include "usblink/transport/posix/PosixSerialTransport.hpp"

int main()
{
    using namespace usblink::transport;

    PosixSerialTransport transport;

    SerialConfig cfg{};
    cfg.portName = "/dev/tty.usbserial-0001";
    cfg.baudRate = 115200;

    const auto opened = transport.open(cfg);
    if (!opened.isOk()) {
        return 1;
    }

    std::array<std::uint8_t, 4> tx{0xAA, 0x55, 0x01, 0x00};
    const auto wr = transport.write(tx);
    if (wr.status != TransportStatus::Ok) {
        return 2;
    }

    std::array<std::uint8_t, 64> rx{};
    const auto rd = transport.read(rx);
    if (rd.status != TransportStatus::Ok && rd.status != TransportStatus::Timeout) {
        return 3;
    }

    // Explicit close is optional; destructor also closes via RAII.
    transport.close();
    return 0;
}
```

## Roadmap

Next milestones after `v0.1`:

- `v0.2`: Add protocol framing (length/type/check fields), parser state machine, and frame-level validation on top of transport
- `v0.3`: Add stability instrumentation (timeout/retry counters, throughput windows, error-bucket aggregation) for long-run runs
- `v0.4`: Add fault-injection and soak harnesses targeting short I/O, `EINTR`, `EAGAIN`, disconnect/reconnect, and prolonged link stress
- `v0.5`: Add backend parity for additional platforms behind `ISerialTransport` with unchanged protocol-layer integration
