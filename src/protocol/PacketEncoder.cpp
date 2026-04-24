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

    size_t findMagicOffset(std::span<const uint8_t> buf, uint32_t magic)
    {
        std::array<uint8_t, sizeof(uint32_t)> m = std::bit_cast<std::array<uint8_t, sizeof(uint32_t)>>(magic);

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

    // Deprecated
    // bool tryParsePacket(std::vector<uint8_t> &buffer, PacketHeader &outHeader, std::vector<uint8_t> &outPayload)
    // {
    //     size_t offset = findMagicOffset(buffer, MAGIC);

    //     if (offset == static_cast<size_t>(-1))
    //         return false;

    //     if (offset > 0)
    //         buffer.erase(buffer.begin(), buffer.begin() + offset);

    //     if (buffer.size() < sizeof(PacketHeader))
    //         return false;

    //     PacketHeader hdr;
    //     std::memcpy(&hdr, buffer.data(), sizeof(PacketHeader));

    //     if (hdr.payloadSize > MAX_PAYLOAD_SIZE)
    //     {
    //         buffer.erase(buffer.begin());
    //         return false;
    //     }

    //     size_t totalSize = sizeof(PacketHeader) + static_cast<size_t>(hdr.payloadSize);

    //     if (totalSize < sizeof(PacketHeader))
    //     {
    //         buffer.erase(buffer.begin());
    //         return false;
    //     }

    //     if (buffer.size() < totalSize)
    //         return false;

    //     outPayload.assign(
    //         buffer.begin() + sizeof(PacketHeader),
    //         buffer.begin() + totalSize);

    //     PacketHeader tempHdr = hdr;
    //     tempHdr.crc = 0;

    //     std::vector<uint8_t> tempBuff;
    //     tempBuff.reserve(totalSize);

    //     std::array<uint8_t, sizeof(PacketHeader)> headerBytes = std::bit_cast<std::array<uint8_t, sizeof(PacketHeader)>>(tempHdr);

    //     tempBuff.insert(tempBuff.end(), headerBytes.begin(), headerBytes.end());

    //     tempBuff.insert(tempBuff.end(), buffer.begin() + sizeof(PacketHeader), buffer.begin() + totalSize);

    //     uint32_t computedCRC = crc32_update(tempBuff);

    //     if (computedCRC != hdr.crc)
    //     {
    //         buffer.erase(buffer.begin());
    //         return false;
    //     }

    //     outHeader = hdr;

    //     buffer.erase(buffer.begin(), buffer.begin() + totalSize);

    //     return true;
    // }

    bool tryParsePacket(std::vector<uint8_t> &buffer, size_t &cursor, ParseState &state, PacketHeader &hdr, PacketHeader &outHeader, std::vector<uint8_t> &outPayload)
    {
        size_t totalSize = 0;

        while (true)
        {
            switch (state)
            {
            case ParseState::SeekMagic:
            {
                if (cursor >= buffer.size())
                    return false;

                size_t offset = findMagicOffset(std::span(buffer.begin() + cursor, buffer.end()), MAGIC);

                if (offset == static_cast<size_t>(-1))
                    return false;

                cursor += offset;
                state = ParseState::WaitHeader;
                break;
            }

            case ParseState::WaitHeader:
            {
                if (buffer.size() - cursor < sizeof(PacketHeader))
                    return false;

                std::memcpy(&hdr, buffer.data() + cursor, sizeof(PacketHeader));

                if (hdr.payloadSize > MAX_PAYLOAD_SIZE)
                {
                    cursor += 1;
                    state = ParseState::SeekMagic;
                    break;
                }

                state = ParseState::WaitPayload;
                break;
            }

            case ParseState::WaitPayload:
            {
                totalSize = sizeof(PacketHeader) + hdr.payloadSize;

                if (buffer.size() - cursor < totalSize)
                    return false;

                state = ParseState::Validate;
                break;
            }

            case ParseState::Validate:
            {
                outPayload.assign(
                    buffer.begin() + cursor + sizeof(PacketHeader),
                    buffer.begin() + cursor + totalSize);

                PacketHeader tempHdr = hdr;
                tempHdr.crc = 0;

                auto hdrBytes = std::bit_cast<std::array<uint8_t, sizeof(PacketHeader)>>(tempHdr);

                uint32_t crc = crc32_update(hdrBytes, 0xFFFFFFFF);
                crc = crc32_update(std::span(buffer.begin() + cursor + sizeof(PacketHeader), buffer.begin() + cursor + totalSize), crc);

                crc = ~crc;

                if (crc != hdr.crc)
                {
                    cursor += 1;
                    state = ParseState::SeekMagic;
                    return false;
                }

                outHeader = hdr;
                cursor += totalSize;

                if (cursor >= COMPACT_THRESHOLD && cursor > buffer.size() / 2)
                {
                    buffer.erase(buffer.begin(), buffer.begin() + cursor);
                    cursor = 0;
                }

                state = ParseState::SeekMagic;
                return true;
            }
            } // switch (state)
        } // while (true)
    }
} // namespace usblink::protocol
