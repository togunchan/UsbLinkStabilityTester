#include "usblink/protocol/PacketEncoder.hpp"

namespace usblink::protocol
{
    std::vector<uint8_t> encodePacket(const PacketHeader &header, std::span<const uint8_t> payload)
    {
        std::vector<uint8_t> buffer;

        // [HEADER][PAYLOAD]
        buffer.reserve(sizeof(PacketHeader) + payload.size());

        // Treat the header as raw bytes.
        // We're just viewing its memory as uint8_t,
        // not copying it (like memcpy would).
        const uint8_t *headerPtr = reinterpret_cast<const uint8_t *>(&header);

        buffer.insert(
            buffer.end(),
            headerPtr,
            headerPtr + sizeof(PacketHeader));

        buffer.insert(
            buffer.end(),
            payload.begin(),
            payload.end());

        return buffer;
    }
} // namespace usblink::protocol
