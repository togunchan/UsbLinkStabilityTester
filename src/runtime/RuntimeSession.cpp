#include "usblink/runtime/RuntimeSession.hpp"
#include "usblink/protocol/Packet.hpp"
#include "usblink/protocol/PacketEncoder.hpp"
#include "usblink/protocol/PayloadGenerator.hpp"
#include <cstdint>
#include <iostream>

namespace usblink
{
    void RuntimeSession::start()
    {
        running_ = true;
        run();
    }

    void RuntimeSession::run()
    {
        while (running_)
        {
            generateAndInjectPacket();
            parseAvailablePackets();

            if (nextSequence_ >= kMaxPacketCount)
            {
                running_ = false;
            }
        }
        printStats();
    }

    void RuntimeSession::generateAndInjectPacket()
    {
        auto payload = protocol::PayloadGenerator::generateIncremental(64);

        protocol::PacketHeader hdr{};
        hdr.magic = protocol::MAGIC;
        hdr.sequence = nextSequence_++;
        hdr.timestamp = 0;

        auto packet = protocol::encodePacket(hdr, payload);

        const bool written = ringBuffer_.write(packet);

        if (!written)
        {
            std::cerr << "RingBuffer overflow\n";
            running_ = false;
            return;
        }
    }

    void RuntimeSession::parseAvailablePackets()
    {
        protocol::PacketHeader parsedHeader{};
        std::vector<uint8_t> parsedPayload;

        while (protocol::tryParsePacket(ringBuffer_, parseState_, workingHeader_, parsedHeader,
                                        parsedPayload, protocolStats_))
        {
            sequenceTracker_.observe(parsedHeader.sequence);
        }
    }

    void RuntimeSession::printStats()
    {
        const auto &seq = sequenceTracker_.stats();

        std::cout << "Valid packets: " << protocolStats_.validPackets << '\n';
        std::cout << "Payload bytes: " << protocolStats_.totalPayloadBytes << '\n';
        std::cout << "Lost packets: " << seq.lostPackets << '\n';
        std::cout << "Duplicates/Reordered: " << seq.duplicateOrReorderedPackets << '\n';
    }

    bool RuntimeSession::isRunning() const noexcept
    {
        return running_;
    }

    const protocol::ProtocolStats &RuntimeSession::protocolStats() const noexcept
    {
        return protocolStats_;
    }

    const protocol::SequenceStats &RuntimeSession::sequenceStats() const noexcept
    {
        return sequenceTracker_.stats();
    }
} // namespace usblink
