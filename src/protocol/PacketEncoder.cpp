#include "usblink/protocol/PacketEncoder.hpp"

namespace usblink::protocol
{
    uint32_t computeCRC32(std::span<const uint8_t> data)
    {
        uint32_t crc = 0xFFFFFFFF;

        for (uint8_t byte : data)
        {
            crc ^= byte;
            for (int i = 0; i < 8; i++)
            {
                if (crc & 1)
                    crc = (crc >> 1) ^ 0xEDB88320;
                else
                    crc >>= 1;
            }
        }

        return ~crc;
    }

    std::vector<uint8_t> encodePacket(const PacketHeader &header, std::span<const uint8_t> payload)
    {
        PacketHeader hdr = header;
        hdr.payloadSize = static_cast<uint32_t>(payload.size());
        hdr.crc = 0;

        std::vector<uint8_t> temp;
        temp.reserve(sizeof(PacketHeader) + payload.size());

        // Treat the header as raw bytes.
        // We're just viewing its memory as uint8_t,
        // not copying it (like memcpy would).
        const uint8_t *tempHeaderPtr = reinterpret_cast<const uint8_t *>(&hdr);

        temp.insert(
            temp.end(),
            tempHeaderPtr,
            tempHeaderPtr + sizeof(PacketHeader));

        temp.insert(
            temp.end(),
            payload.begin(),
            payload.end());

        uint32_t crc = computeCRC32(temp);

        hdr.crc = crc;

        std::vector<uint8_t> buffer;
        // [HEADER][PAYLOAD]
        buffer.reserve(sizeof(PacketHeader) + payload.size());

        const uint8_t *finalHeaderPtr = reinterpret_cast<const uint8_t *>(&hdr);

        buffer.insert(
            buffer.end(),
            finalHeaderPtr,
            finalHeaderPtr + sizeof(PacketHeader));

        buffer.insert(
            buffer.end(),
            payload.begin(),
            payload.end());

        return buffer;
    }
} // namespace usblink::protocol
