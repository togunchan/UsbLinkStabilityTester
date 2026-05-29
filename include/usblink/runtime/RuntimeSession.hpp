#pragma once

#include "usblink/core/RingBuffer.hpp"
#include "usblink/protocol/Packet.hpp"
#include "usblink/protocol/PacketEncoder.hpp"
#include "usblink/protocol/ProtocolStats.hpp"
#include "usblink/protocol/SequenceTracker.hpp"

#include <cstdint>

namespace usblink
{
    class RuntimeSession
    {
    public:
        RuntimeSession() = default;

        void start();
        void stop();
        void run();
        void generateAndInjectPacket();
        void parseAvailablePackets();
        void printStats();

        [[nodiscard]] bool isRunning() const noexcept;
        [[nodiscard]] const protocol::ProtocolStats &protocolStats() const noexcept;
        [[nodiscard]] const protocol::SequenceStats &sequenceStats() const noexcept;

    private:
        bool running_{false};
        core::RingBuffer ringBuffer_{8192};
        protocol::ParseState parseState_{protocol::ParseState::SeekMagic};
        protocol::PacketHeader workingHeader_{};
        protocol::ProtocolStats protocolStats_{};
        protocol::SequenceTracker sequenceTracker_{};
        static constexpr uint32_t kMaxPacketCount = 100;
        uint32_t nextSequence_{0};
    };
} // namespace usblink
