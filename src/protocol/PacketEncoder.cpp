#include "usblink/protocol/PacketEncoder.hpp"

namespace usblink::protocol
{
    static uint32_t computeCRC32(std::span<const uint8_t> data)
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

    size_t findMagicOffset(std::span<const uint8_t> buf, uint32_t magic)
    {
        // convert "uint32_t (4 btye) magic" into byte array
        const uint8_t *m = reinterpret_cast<const uint8_t *>(&magic);

        if (buf.size() < sizeof(uint32_t))
            return static_cast<size_t>(-1);

        for (size_t i = 0; i <= buf.size() - sizeof(uint32_t); ++i)
        {
            if (buf[i] == m[0] &&
                buf[i + 1] == m[1] &&
                buf[i + 2] == m[2] &&
                buf[i + 3] == m[3])
            {
                return i;
            }
        }

        // -1 cast to size_t becomes max value (all bits set to 1) -> used as "not found"
        return static_cast<size_t>(-1);
    }

    bool tryParsePacket(std::vector<uint8_t> &buffer, PacketHeader &outHeader, std::vector<uint8_t> &outPayload)
    {
        size_t offset = findMagicOffset(buffer, MAGIC);

        if (offset == static_cast<size_t>(-1))
            return false;

        if (offset > 0)
            buffer.erase(buffer.begin(), buffer.begin() + offset);

        if (buffer.size() < sizeof(PacketHeader))
            return false;

        PacketHeader hdr;
        std::memcpy(&hdr, buffer.data(), sizeof(PacketHeader));

        size_t totalSize = sizeof(PacketHeader) + hdr.payloadSize;

        if (buffer.size() < totalSize)
            return false;

        outPayload.assign(
            buffer.begin() + sizeof(PacketHeader),
            buffer.begin() + totalSize);

        outHeader = hdr;

        buffer.erase(buffer.begin(), buffer.begin() + totalSize);

        return true;
    }
} // namespace usblink::protocol
