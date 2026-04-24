#pragma once

#include <cstddef>
#include <cstdint>

namespace usblink::protocol
{
    constexpr uint32_t MAGIC = 0xAABBCCDD;
    constexpr size_t MAX_PAYLOAD_SIZE = 4096;
    constexpr size_t COMPACT_THRESHOLD = 1024;

// Ensure deterministic byte layout for serialization (no padding between fields)
// Without this, the compiler may insert hidden bytes, breaking protocol parsing
#pragma pack(push, 1)
    struct PacketHeader
    {
        uint32_t magic;
        uint32_t sequence;
        uint64_t timestamp;
        uint32_t payloadSize;
        uint32_t crc;
    };
#pragma pack(pop)
} // namespace usblink::protocol
