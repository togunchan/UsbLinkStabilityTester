#pragma once

#include "Packet.hpp"

#include <vector>
#include <span>

namespace usblink::protocol
{
    enum class ParseState
    {
        SeekMagic,
        WaitHeader,
        WaitPayload,
        Validate
    };

    std::vector<uint8_t> encodePacket(const PacketHeader &header, std::span<const uint8_t> payload);

    size_t findMagicOffset(std::span<const uint8_t> buf, uint32_t magic);

    bool tryParsePacket(std::vector<uint8_t> &buffer, PacketHeader &outHeader, std::vector<uint8_t> &outPayload);

    bool tryParsePacket(std::vector<uint8_t> &buffer, size_t &cursor, ParseState &state, PacketHeader &outHeader, std::vector<uint8_t> &outPayload);

} // namespace usblink::protocol
