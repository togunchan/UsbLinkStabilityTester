#pragma once

#include <cstdint>

namespace usblink::protocol
{
    struct SequenceStats
    {
        uint64_t receivedPackets{0}; // how many valid packets we have seen
        uint64_t lostPackets{0};     // sequence gap
        uint64_t duplicateOrReorderedPackets{0};
    };

    class SequenceTracker
    {
    public:
        void observe(uint32_t sequence);

        [[nodiscard]] const SequenceStats &stats() const noexcept;
        [[nodiscard]] bool initialized() const noexcept;
        [[nodiscard]] uint32_t expectedSequence() const noexcept;

    private:
        bool initialized_{false};      // has the first packet arrived
        uint32_t expectedSequence_{0}; // which sequence is expected next?
        SequenceStats stats_{};
    };
} // namespace usblink::protocol
