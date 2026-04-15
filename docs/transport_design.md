# Serial Transport Layer Design

## Overview
The serial transport layer provides a platform-independent API for byte-stream communication over USB virtual serial devices (for example STM32 CDC and Arduino USB serial). Its purpose is to isolate OS- and driver-specific details from higher-level application logic so protocol code remains portable, testable, and maintainable.

## `ISerialTransport` Interface
`ISerialTransport` is the contract between application code and concrete transport backends.

Why an interface is used:
- Separates policy (application/protocol logic) from mechanism (POSIX/Win32 serial APIs).
- Enables multiple implementations behind one API (real device, mock/fake for tests).
- Reduces platform conditionals in higher layers.

What it abstracts:
- Device handle lifecycle.
- Blocking/non-blocking byte-stream I/O operations.
- Connection state queries independent of backend details.

## Responsibilities
`ISerialTransport` implementations are responsible for:
- `open(...)`: establish a serial connection with the configured device/port settings.
- `close()`: release all underlying OS resources deterministically.
- `read(span)`: pull bytes from the transport into caller-provided memory.
- `write(span)`: push caller-provided bytes to the transport.
- `isOpen()`: report whether the transport currently owns a valid, usable connection.

## Key Design Decisions
- `TransportResult` instead of `bool`: return values must encode more than success/failure (for example EOF, timeout, interrupted call, OS error). This supports actionable error handling and clearer diagnostics.
- `std::span` for read/write buffers: provides a non-owning view over contiguous memory with explicit size, avoids raw pointer/length mismatch, and works with arrays, `std::vector`, and fixed buffers without copies.
- `isOpen() const`: state queries must be side-effect free; `const` enforces this contract and allows checks through const-qualified references.
- Virtual destructor: interface objects are used polymorphically; a virtual destructor guarantees correct derived-class cleanup when deleted via an `ISerialTransport*` or smart pointer to the interface.

## Abstraction Layers
```text
[ Application ]
       |
       v
[ ISerialTransport ]
       |
       v
[ POSIX / Win32 Serial Backend ]
       |
       v
[ OS Virtual COM Driver ]
       |
       v
[ USB CDC Device (STM32 / Arduino) ]
```
