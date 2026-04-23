#pragma once

#include "Packet.hpp"

#include <vector>
#include <span>

namespace usblink::protocol
{
    std::vector<uint8_t> encodePacket(const PacketHeader &header, std::span<const uint8_t> payload);

} // namespace usblink::protocol
