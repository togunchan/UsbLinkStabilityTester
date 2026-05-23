#pragma once

#include <cstdint>

namespace usblink::protocol
{
    struct ProtocolStats
    {
        uint64_t parsedPackets{0};
        uint64_t crcFailures{0};
        uint64_t malformedHeaders{0};
        uint64_t resyncEvents{0};
        uint64_t discardedBytes{0};

        uint64_t bytesReceived{0};
        uint64_t validPackets{0};
        uint64_t invalidPackets{0};
        uint64_t totalPayloadBytes{0};
    };
} // namespace usblink::protocol