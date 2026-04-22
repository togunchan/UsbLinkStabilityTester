#pragma once

#include <cstdint>

namespace usblink::protocol
{
    constexpr uint32_t MAGIC = 0xAABBCCDD;

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
}