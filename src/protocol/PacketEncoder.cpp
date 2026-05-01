#include "usblink/protocol/PacketEncoder.hpp"

#include <cstring>
#include <bit>
#include <array>

namespace usblink::protocol
{
    static uint32_t crc32_update(std::span<const uint8_t> data, uint32_t crc = 0xFFFFFFFF)
    {
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
        return crc;
    }

    std::vector<uint8_t> encodePacket(const PacketHeader &header, std::span<const uint8_t> payload)
    {
        PacketHeader hdr = header;
        hdr.payloadSize = static_cast<uint32_t>(payload.size());
        hdr.crc = 0;

        std::array<uint8_t, sizeof(PacketHeader)> headerBytes = std::bit_cast<std::array<uint8_t, sizeof(PacketHeader)>>(hdr);

        uint32_t crc = crc32_update(headerBytes, 0xFFFFFFFF);
        crc = crc32_update(payload, crc);
        crc = ~crc;

        hdr.crc = crc;

        std::vector<uint8_t> buffer;
        // [HEADER][PAYLOAD]
        buffer.reserve(sizeof(PacketHeader) + payload.size());

        std::array<uint8_t, sizeof(PacketHeader)> finalHeaderBytes = std::bit_cast<std::array<uint8_t, sizeof(PacketHeader)>>(hdr);

        buffer.insert(buffer.end(), finalHeaderBytes.begin(), finalHeaderBytes.end());
        buffer.insert(buffer.end(), payload.begin(), payload.end());

        return buffer;
    }

    size_t findMagicOffset(const core::RingBuffer &buffer, uint32_t magic)
    {
        std::array<uint8_t, sizeof(uint32_t)> m = std::bit_cast<std::array<uint8_t, sizeof(uint32_t)>>(magic);

        if (buffer.size() < sizeof(uint32_t))
            return std::numeric_limits<size_t>::max();

        for (size_t i = 0; i <= buffer.size() - sizeof(uint32_t); ++i)
        {
            if (buffer[i] == m[0] &&
                buffer[i + 1] == m[1] &&
                buffer[i + 2] == m[2] &&
                buffer[i + 3] == m[3])
            {
                return i;
            }
        }

        // -1 cast to size_t becomes max value (all bits set to 1) -> used as "not found"
        return std::numeric_limits<size_t>::max();
    }

    bool tryParsePacket(core::RingBuffer &buffer, ParseState &state, PacketHeader &hdr, PacketHeader &outHeader, std::vector<uint8_t> &outPayload)
    {
        size_t totalSize = 0;

        while (true)
        {
            switch (state)
            {
            case ParseState::SeekMagic:
            {
                size_t offset = findMagicOffset(buffer, MAGIC);

                if (offset == std::numeric_limits<size_t>::max())
                    return false;

                buffer.consume(offset);
                state = ParseState::WaitHeader;
                break;
            }

            case ParseState::WaitHeader:
            {
                if (buffer.size() < sizeof(PacketHeader))
                    return false;

                std::array<uint8_t, sizeof(PacketHeader)> tmp;

                for (size_t i = 0; i < tmp.size(); i++)
                    tmp[i] = buffer[i];

                hdr = std::bit_cast<PacketHeader>(tmp);

                if (hdr.payloadSize > MAX_PAYLOAD_SIZE)
                {
                    buffer.consume(1);
                    state = ParseState::SeekMagic;
                    break;
                }

                state = ParseState::WaitPayload;
                break;
            }

            case ParseState::WaitPayload:
            {
                totalSize = sizeof(PacketHeader) + hdr.payloadSize;

                if (buffer.size() < totalSize)
                    return false;

                state = ParseState::Validate;
                break;
            }

            case ParseState::Validate:
            {
                outPayload.clear();
                outPayload.reserve(hdr.payloadSize);

                for (size_t i = 0; i < hdr.payloadSize; ++i)
                {
                    outPayload.push_back(buffer[sizeof(PacketHeader) + i]);
                }

                PacketHeader tempHdr = hdr;
                tempHdr.crc = 0;

                auto hdrBytes = std::bit_cast<std::array<uint8_t, sizeof(PacketHeader)>>(tempHdr);

                uint32_t crc = crc32_update(hdrBytes, 0xFFFFFFFF);
                for (size_t i = 0; i < hdr.payloadSize; ++i)
                {
                    uint8_t b = buffer[sizeof(PacketHeader) + i];
                    crc = crc32_update(std::span(&b, 1), crc);
                }
                crc = ~crc;

                if (crc != hdr.crc)
                {
                    buffer.consume(1);
                    state = ParseState::SeekMagic;
                    return false;
                }

                outHeader = hdr;
                buffer.consume(totalSize);

                state = ParseState::SeekMagic;
                return true;
            }
            } // switch (state)
        } // while (true)
    }
} // namespace usblink::protocol
