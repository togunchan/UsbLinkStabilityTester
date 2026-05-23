#include "usblink/protocol/SequenceTracker.hpp"
#include <cstdint>

namespace usblink::protocol
{
    void SequenceTracker::observe(uint32_t sequence)
    {
        if (!initialized_)
        {
            initialized_ = true;
            expectedSequence_ = sequence + 1;
            stats_.receivedPackets++;
            return;
        }

        if (sequence == expectedSequence_)
        {
            stats_.receivedPackets++;
            expectedSequence_ = sequence + 1;
            return;
        }

        if (sequence > expectedSequence_)
        {
            stats_.lostPackets += (sequence - expectedSequence_);
            stats_.receivedPackets++;
            expectedSequence_ = sequence + 1;
            return;
        }

        stats_.duplicateOrReorderedPackets++;
    }

    const SequenceStats &SequenceTracker::stats() const noexcept
    {
        return stats_;
    }

    bool SequenceTracker::initialized() const noexcept
    {
        return initialized_;
    }

    uint32_t SequenceTracker::expectedSequence() const noexcept
    {
        return expectedSequence_;
    }
} // namespace usblink::protocol
