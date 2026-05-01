#pragma once

#include "Packet.hpp"
#include "usblink/core/RingBuffer.hpp"

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

    size_t findMagicOffset(const core::RingBuffer &buffer, uint32_t magic);

    bool tryParsePacket(core::RingBuffer &buffer, ParseState &state, PacketHeader &hdr, PacketHeader &outHeader, std::vector<uint8_t> &outPayload);

} // namespace usblink::protocol
