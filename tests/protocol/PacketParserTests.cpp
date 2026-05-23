#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#elif __has_include(<catch2/catch.hpp>)
#include <catch2/catch.hpp>
#else
#error "Catch2 headers not found"
#endif

#include "usblink/core/RingBuffer.hpp"
#include "usblink/protocol/PacketEncoder.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace
{
    using usblink::core::RingBuffer;
    using usblink::protocol::MAGIC;
    using usblink::protocol::MAX_PAYLOAD_SIZE;
    using usblink::protocol::ParseState;
    using usblink::protocol::PacketHeader;
    using usblink::protocol::ProtocolStats;

    PacketHeader makeHeader(uint32_t sequence, uint64_t timestamp)
    {
        return PacketHeader{
            .magic = MAGIC,
            .sequence = sequence,
            .timestamp = timestamp,
            .payloadSize = 0,
            .crc = 0,
        };
    }

    std::vector<uint8_t> makePacket(uint32_t sequence, uint64_t timestamp, std::span<const uint8_t> payload)
    {
        return usblink::protocol::encodePacket(makeHeader(sequence, timestamp), payload);
    }

    std::vector<uint8_t> makePayloadCorruptedPacket(uint32_t sequence, uint64_t timestamp,
                                                    std::span<const uint8_t> payload, uint8_t mask)
    {
        std::vector<uint8_t> packet = makePacket(sequence, timestamp, payload);
        packet[sizeof(PacketHeader)] ^= mask;
        return packet;
    }

    std::vector<uint8_t> makeOversizedHeaderPacket(uint32_t sequence, uint64_t timestamp,
                                                   std::span<const uint8_t> payload)
    {
        std::vector<uint8_t> packet = makePacket(sequence, timestamp, payload);
        constexpr std::size_t payloadSizeOffset = sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint64_t);
        const uint32_t oversized = static_cast<uint32_t>(MAX_PAYLOAD_SIZE + 1);
        const uint8_t *oversizedBytes = reinterpret_cast<const uint8_t *>(&oversized);

        for (std::size_t i = 0; i < sizeof(uint32_t); ++i)
            packet[payloadSizeOffset + i] = oversizedBytes[i];

        return packet;
    }

} // namespace

TEST_CASE("tryParsePacket parses a complete valid packet", "[protocol][parser]")
{
    const std::vector<uint8_t> payload{0x10, 0x20, 0x30, 0x40, 0x50};
    const auto encoded = makePacket(42, 1'700'000'000ULL, payload);

    RingBuffer rb(encoded.size());
    REQUIRE(rb.write(encoded));
    PacketHeader workingHeader{};
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    ProtocolStats stats{};
    ParseState state = ParseState::SeekMagic;

    REQUIRE(usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats));
    REQUIRE(parsedHeader.magic == MAGIC);
    REQUIRE(parsedHeader.sequence == 42);
    REQUIRE(parsedHeader.timestamp == 1'700'000'000ULL);
    REQUIRE(parsedHeader.payloadSize == payload.size());
    REQUIRE(parsedPayload == payload);
    REQUIRE(rb.size() == 0);
}

TEST_CASE("tryParsePacket waits when only a partial header is available", "[protocol][parser]")
{
    const std::vector<uint8_t> payload{0x01, 0x02, 0x03};
    const auto encoded = makePacket(7, 777ULL, payload);

    const std::size_t partialHeaderSize = sizeof(PacketHeader) - 3;
    REQUIRE(partialHeaderSize < sizeof(PacketHeader));

    RingBuffer rb(encoded.size());
    REQUIRE(rb.write(std::span<const uint8_t>(encoded.data(), partialHeaderSize)));

    PacketHeader workingHeader{};
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    ProtocolStats stats{};
    ParseState state = ParseState::SeekMagic;

    REQUIRE_FALSE(usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats));
    REQUIRE(state == ParseState::WaitHeader);
    REQUIRE(rb.size() == partialHeaderSize);
}

TEST_CASE("tryParsePacket waits for remaining payload bytes", "[protocol][parser]")
{
    const std::vector<uint8_t> payload{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xF0};
    const auto encoded = makePacket(8, 8'888ULL, payload);

    const std::size_t incompleteSize = sizeof(PacketHeader) + payload.size() - 2;

    RingBuffer rb(encoded.size());
    REQUIRE(rb.write(std::span<const uint8_t>(encoded.data(), incompleteSize)));

    PacketHeader workingHeader{};
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    ProtocolStats stats{};
    ParseState state = ParseState::SeekMagic;

    REQUIRE_FALSE(usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats));
    REQUIRE(state == ParseState::WaitPayload);
    REQUIRE(rb.size() == incompleteSize);

    REQUIRE(rb.write(std::span<const uint8_t>(
        encoded.data() + incompleteSize,
        encoded.size() - incompleteSize)));

    REQUIRE(usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats));
    REQUIRE(parsedHeader.sequence == 8);
    REQUIRE(parsedHeader.timestamp == 8'888ULL);
    REQUIRE(parsedHeader.payloadSize == payload.size());
    REQUIRE(parsedPayload == payload);
    REQUIRE(rb.size() == 0);
}

TEST_CASE("tryParsePacket skips garbage bytes before a valid packet", "[protocol][parser]")
{
    const std::vector<uint8_t> payload{0x44, 0x55, 0x66};
    const auto encoded = makePacket(9, 9'999ULL, payload);

    const std::vector<uint8_t> garbage{0x13, 0x37, 0xBA, 0xAD, 0xF0, 0x0D};
    RingBuffer rb(garbage.size() + encoded.size());
    REQUIRE(rb.write(garbage));

    PacketHeader workingHeader{};
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    ProtocolStats stats{};
    ParseState state = ParseState::SeekMagic;

    REQUIRE_FALSE(usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats));
    REQUIRE(state == ParseState::SeekMagic);
    REQUIRE(rb.size() == sizeof(uint32_t) - 1U);

    REQUIRE(rb.write(encoded));

    REQUIRE(usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats));
    REQUIRE(parsedHeader.sequence == 9);
    REQUIRE(parsedPayload == payload);
    REQUIRE(rb.size() == 0);
}

TEST_CASE("tryParsePacket rejects CRC-mismatched packet and resynchronizes", "[protocol][parser]")
{
    const std::vector<uint8_t> badPayload{0x01, 0x03, 0x05, 0x07, 0x09};
    auto corruptedPacket = makePacket(10, 10'101ULL, badPayload);
    corruptedPacket[sizeof(PacketHeader) + 1] ^= 0xFF; // break CRC

    const std::vector<uint8_t> goodPayload{0xDE, 0xAD, 0xBE, 0xEF};
    const auto validPacket = makePacket(11, 11'111ULL, goodPayload);

    RingBuffer rb(corruptedPacket.size() + validPacket.size());
    REQUIRE(rb.write(corruptedPacket));
    REQUIRE(rb.write(validPacket));

    PacketHeader workingHeader{};
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    ProtocolStats stats{};
    ParseState state = ParseState::SeekMagic;

    const std::size_t initialSize = rb.size();
    REQUIRE_FALSE(usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats));
    REQUIRE(rb.size() == initialSize - 1); // one-byte drop resync policy
    REQUIRE(state == ParseState::SeekMagic);

    bool recovered = false;
    std::size_t attempts = 0;
    const std::size_t maxAttempts = initialSize * 2;

    while (!recovered && attempts < maxAttempts)
    {
        recovered = usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats);
        attempts++;
    }

    REQUIRE(recovered);
    REQUIRE(parsedHeader.sequence == 11);
    REQUIRE(parsedHeader.timestamp == 11'111ULL);
    REQUIRE(parsedHeader.payloadSize == goodPayload.size());
    REQUIRE(parsedPayload == goodPayload);
    REQUIRE(rb.size() == 0);
}

TEST_CASE("tryParsePacket returns at most one packet per call", "[protocol][parser]")
{
    const std::vector<uint8_t> payload1{0x01, 0x02};
    const std::vector<uint8_t> payload2{0xA0, 0xB0, 0xC0};

    const auto packet1 = makePacket(100, 1000ULL, payload1);
    const auto packet2 = makePacket(101, 1001ULL, payload2);

    RingBuffer rb(packet1.size() + packet2.size());
    REQUIRE(rb.write(packet1));
    REQUIRE(rb.write(packet2));

    ParseState state = ParseState::SeekMagic;
    PacketHeader workingHeader{};
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    ProtocolStats stats{};

    const std::array<uint32_t, 2> expectedSequences{100, 101};
    const std::array<std::vector<uint8_t>, 2> expectedPayloads{payload1, payload2};

    for (std::size_t i = 0; i < expectedSequences.size(); ++i)
    {
        const size_t before = rb.size();
        REQUIRE(usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats));
        REQUIRE(rb.size() < before);
        REQUIRE(parsedHeader.sequence == expectedSequences[i]);
        REQUIRE(parsedPayload == expectedPayloads[i]);
        if (i == 0)
            REQUIRE(rb.size() > 0);
    }

    REQUIRE(rb.size() == 0);
}

TEST_CASE("tryParsePacket handles stream-split input and succeeds only when complete", "[protocol][parser]")
{
    const std::vector<uint8_t> payload{0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    const auto encoded = makePacket(555, 55'555ULL, payload);

    const std::array<std::size_t, 5> chunkSizes{1, 4, 3, 2, 64};

    RingBuffer rb(encoded.size());
    ParseState state = ParseState::SeekMagic;
    PacketHeader workingHeader{};
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    ProtocolStats stats{};

    std::size_t consumed = 0;

    for (std::size_t chunkSize : chunkSizes)
    {
        if (consumed >= encoded.size())
            break;

        const std::size_t bytesToCopy = std::min(chunkSize, encoded.size() - consumed);
        REQUIRE(rb.write(std::span<const uint8_t>(
            encoded.data() + consumed,
            bytesToCopy)));
        consumed += bytesToCopy;

        const bool parsed = usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats);

        if (consumed < encoded.size())
            REQUIRE_FALSE(parsed);
        else
            REQUIRE(parsed);
    }

    REQUIRE(consumed == encoded.size());
    REQUIRE(parsedHeader.sequence == 555);
    REQUIRE(parsedHeader.timestamp == 55'555ULL);
    REQUIRE(parsedHeader.payloadSize == payload.size());
    REQUIRE(parsedPayload == payload);
    REQUIRE(rb.size() == 0);
}

TEST_CASE("tryParsePacket does not treat magic bytes inside payload as a packet boundary", "[protocol][parser]")
{
    const uint8_t *magicBytes = reinterpret_cast<const uint8_t *>(&MAGIC);

    const std::vector<uint8_t> payloadWithMagic{
        0x01,
        magicBytes[0], magicBytes[1], magicBytes[2], magicBytes[3],
        0x02, 0x03,
        magicBytes[0], magicBytes[1], magicBytes[2], magicBytes[3],
        0x04, 0x05};
    const std::vector<uint8_t> trailingPayload{0xA0, 0xB0, 0xC0};

    const auto firstPacket = makePacket(600, 60'000ULL, payloadWithMagic);
    const auto secondPacket = makePacket(601, 60'001ULL, trailingPayload);

    const std::size_t splitPoint = sizeof(PacketHeader) + 6;
    RingBuffer rb(firstPacket.size() + secondPacket.size());
    REQUIRE(rb.write(std::span<const uint8_t>(firstPacket.data(), splitPoint)));

    ParseState state = ParseState::SeekMagic;
    PacketHeader workingHeader{};
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    ProtocolStats stats{};

    REQUIRE_FALSE(usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats));
    REQUIRE(rb.size() == splitPoint);

    REQUIRE(rb.write(std::span<const uint8_t>(firstPacket.data() + splitPoint, firstPacket.size() - splitPoint)));
    REQUIRE(rb.write(secondPacket));

    const std::array<uint32_t, 2> expectedSequences{600, 601};
    const std::array<std::vector<uint8_t>, 2> expectedPayloads{payloadWithMagic, trailingPayload};

    for (std::size_t i = 0; i < expectedSequences.size(); ++i)
    {
        const size_t before = rb.size();
        REQUIRE(usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats));
        REQUIRE(rb.size() < before);
        REQUIRE(parsedHeader.sequence == expectedSequences[i]);
        REQUIRE(parsedPayload == expectedPayloads[i]);
    }

    REQUIRE(rb.size() == 0);
}

TEST_CASE("tryParsePacket parses zero-length payload packets", "[protocol][parser]")
{
    const std::vector<uint8_t> emptyPayload;
    const auto encoded = makePacket(610, 61'000ULL, emptyPayload);

    REQUIRE(encoded.size() == sizeof(PacketHeader));

    RingBuffer rb(encoded.size());
    REQUIRE(rb.write(encoded));
    PacketHeader workingHeader{};
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload{0xFF}; // ensure parser overwrites output state
    ProtocolStats stats{};
    ParseState state = ParseState::SeekMagic;

    REQUIRE(usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats));
    REQUIRE(parsedHeader.sequence == 610);
    REQUIRE(parsedHeader.payloadSize == 0);
    REQUIRE(parsedPayload.empty());
    REQUIRE(rb.size() == 0);
}

TEST_CASE("tryParsePacket accepts payload at MAX_PAYLOAD_SIZE boundary", "[protocol][parser]")
{
    std::vector<uint8_t> maxPayload(MAX_PAYLOAD_SIZE);
    for (std::size_t i = 0; i < maxPayload.size(); ++i)
        maxPayload[i] = static_cast<uint8_t>((i * 13U + 7U) & 0xFFU);

    const auto encoded = makePacket(615, 61'500ULL, maxPayload);

    RingBuffer rb(encoded.size());
    REQUIRE(rb.write(encoded));
    PacketHeader workingHeader{};
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    ProtocolStats stats{};
    ParseState state = ParseState::SeekMagic;

    REQUIRE(usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats));
    REQUIRE(parsedHeader.sequence == 615);
    REQUIRE(parsedHeader.payloadSize == MAX_PAYLOAD_SIZE);
    REQUIRE(parsedPayload == maxPayload);
    REQUIRE(rb.size() == 0);
}

TEST_CASE("tryParsePacket rejects payloadSize above MAX_PAYLOAD_SIZE and resynchronizes", "[protocol][parser]")
{
    const std::vector<uint8_t> basePayload{0xAA, 0xBB, 0xCC, 0xDD};
    const auto oversizedHeaderPacket = makeOversizedHeaderPacket(616, 61'600ULL, basePayload);

    const std::vector<uint8_t> validPayload{0x01, 0x03, 0x05};
    const auto validPacket = makePacket(617, 61'700ULL, validPayload);

    RingBuffer rb(oversizedHeaderPacket.size() + validPacket.size());
    REQUIRE(rb.write(oversizedHeaderPacket));

    ParseState state = ParseState::SeekMagic;
    PacketHeader workingHeader{};
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    ProtocolStats stats{};

    REQUIRE_FALSE(usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats));
    REQUIRE(rb.size() == sizeof(uint32_t) - 1U);
    REQUIRE(state == ParseState::SeekMagic);

    REQUIRE(rb.write(validPacket));

    bool recovered = false;
    std::size_t attempts = 0;
    const std::size_t maxAttempts = rb.size() * 2;
    while (!recovered && attempts < maxAttempts)
    {
        recovered = usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats);
        attempts++;
    }

    REQUIRE(recovered);
    REQUIRE(parsedHeader.sequence == 617);
    REQUIRE(parsedHeader.payloadSize == validPayload.size());
    REQUIRE(parsedPayload == validPayload);
    REQUIRE(rb.size() == 0);
}

TEST_CASE("tryParsePacket correctly parses a large payload stream", "[protocol][parser]")
{
    std::vector<uint8_t> largePayload(2048);
    for (std::size_t i = 0; i < largePayload.size(); ++i)
        largePayload[i] = static_cast<uint8_t>((i * 37U + 11U) & 0xFFU);

    const auto encoded = makePacket(620, 62'000ULL, largePayload);

    RingBuffer rb(encoded.size());
    ParseState state = ParseState::SeekMagic;
    PacketHeader workingHeader{};
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    ProtocolStats stats{};

    std::size_t consumed = 0;
    constexpr std::size_t chunkSize = 113;

    while (consumed < encoded.size())
    {
        const std::size_t bytesToCopy = std::min(chunkSize, encoded.size() - consumed);
        REQUIRE(rb.write(std::span<const uint8_t>(encoded.data() + consumed, bytesToCopy)));
        consumed += bytesToCopy;

        const bool parsed = usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats);
        if (consumed < encoded.size())
            REQUIRE_FALSE(parsed);
        else
            REQUIRE(parsed);
    }

    REQUIRE(parsedHeader.sequence == 620);
    REQUIRE(parsedHeader.payloadSize == largePayload.size());
    REQUIRE(parsedPayload == largePayload);
    REQUIRE(rb.size() == 0);
}

TEST_CASE("tryParsePacket recovers after multiple consecutive CRC failures", "[protocol][parser]")
{
    const std::vector<uint8_t> badPayload1{0x01, 0x02, 0x03, 0x04};
    const std::vector<uint8_t> badPayload2{0x10, 0x11, 0x12, 0x13, 0x14};
    const std::vector<uint8_t> badPayload3{0x21, 0x22, 0x23, 0x24, 0x25, 0x26};
    const std::vector<uint8_t> goodPayload{0xDE, 0xAD, 0xFA, 0xCE};

    const auto badPacket1 = makePayloadCorruptedPacket(630, 63'000ULL, badPayload1, 0x11);
    const auto badPacket2 = makePayloadCorruptedPacket(631, 63'001ULL, badPayload2, 0x22);
    const auto badPacket3 = makePayloadCorruptedPacket(632, 63'002ULL, badPayload3, 0x33);
    const auto goodPacket = makePacket(633, 63'003ULL, goodPayload);

    RingBuffer rb(badPacket1.size() + badPacket2.size() + badPacket3.size() + goodPacket.size());
    REQUIRE(rb.write(badPacket1));
    REQUIRE(rb.write(badPacket2));
    REQUIRE(rb.write(badPacket3));
    REQUIRE(rb.write(goodPacket));

    ParseState state = ParseState::SeekMagic;
    PacketHeader workingHeader{};
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    ProtocolStats stats{};

    const std::size_t initialSize = rb.size();
    REQUIRE_FALSE(usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats));
    REQUIRE(rb.size() == initialSize - 1);
    REQUIRE(state == ParseState::SeekMagic);

    bool recovered = false;
    std::size_t failedAttempts = 1;
    std::size_t attempts = 1;
    const std::size_t maxAttempts = rb.size() * 2;

    while (!recovered && attempts < maxAttempts)
    {
        recovered = usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats);
        if (!recovered)
            failedAttempts++;
        attempts++;
    }

    REQUIRE(recovered);
    REQUIRE(failedAttempts >= 3);
    REQUIRE(parsedHeader.sequence == 633);
    REQUIRE(parsedPayload == goodPayload);
    REQUIRE(rb.size() == 0);
}

TEST_CASE("tryParsePacket remains stable when header fields are corrupted", "[protocol][parser]")
{
    const std::vector<uint8_t> payloadA{0x31, 0x32, 0x33, 0x34};
    const std::vector<uint8_t> payloadB{0x41, 0x42, 0x43, 0x44, 0x45};
    const std::vector<uint8_t> payloadC{0x51, 0x52, 0x53};

    auto badMagicPacket = makePacket(640, 64'000ULL, payloadA);
    badMagicPacket[0] ^= 0x80; // corrupt magic to break alignment

    auto badSizePacket = makePacket(641, 64'001ULL, payloadB);
    constexpr std::size_t payloadSizeOffset = sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint64_t);
    bool adjusted = false;
    for (std::size_t i = 0; i < sizeof(uint32_t); ++i)
    {
        uint8_t &sizeByte = badSizePacket[payloadSizeOffset + i];
        if (sizeByte != 0)
        {
            sizeByte = static_cast<uint8_t>(sizeByte + 3U);
            adjusted = true;
            break;
        }
    }
    if (!adjusted)
        badSizePacket[payloadSizeOffset] = 3;

    const auto validPacket = makePacket(642, 64'002ULL, payloadC);

    RingBuffer rb(badMagicPacket.size() + badSizePacket.size() + validPacket.size());
    REQUIRE(rb.write(badMagicPacket));
    REQUIRE(rb.write(badSizePacket));
    REQUIRE(rb.write(validPacket));

    ParseState state = ParseState::SeekMagic;
    PacketHeader workingHeader{};
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    ProtocolStats stats{};

    bool parsed = false;
    std::size_t attempts = 0;
    const std::size_t maxAttempts = rb.size() * 2;
    while (!parsed && attempts < maxAttempts)
    {
        parsed = usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats);
        attempts++;
    }

    REQUIRE(parsed);
    REQUIRE(parsedHeader.sequence == 642);
    REQUIRE(parsedPayload == payloadC);
    REQUIRE(rb.size() == 0);
}

TEST_CASE("tryParsePacket handles exact buffer boundary sizes without off-by-one errors", "[protocol][parser]")
{
    const std::vector<uint8_t> payload{0xAA, 0xBB, 0xCC, 0xDD};
    const auto encoded = makePacket(650, 65'000ULL, payload);

    RingBuffer rb(encoded.size());
    REQUIRE(rb.write(std::span<const uint8_t>(encoded.data(), sizeof(PacketHeader))));
    REQUIRE(rb.size() == sizeof(PacketHeader));

    ParseState state = ParseState::SeekMagic;
    PacketHeader workingHeader{};
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    ProtocolStats stats{};

    REQUIRE_FALSE(usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats));
    REQUIRE(rb.size() == sizeof(PacketHeader));

    REQUIRE(rb.write(std::span<const uint8_t>(encoded.data() + sizeof(PacketHeader), payload.size())));
    REQUIRE(rb.size() == encoded.size());

    REQUIRE(usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats));
    REQUIRE(parsedHeader.sequence == 650);
    REQUIRE(parsedHeader.payloadSize == payload.size());
    REQUIRE(parsedPayload == payload);
    REQUIRE(rb.size() == 0);
}

TEST_CASE("tryParsePacket recovers from deterministic random noise between valid packets", "[protocol][parser]")
{
    auto makeNoise = [](std::size_t count, uint32_t seed)
    {
        std::vector<uint8_t> noise;
        noise.reserve(count);
        uint32_t state = seed;
        for (std::size_t i = 0; i < count; ++i)
        {
            state = state * 1664525U + 1013904223U;
            noise.push_back(static_cast<uint8_t>((state >> 24) & 0xFFU));
        }
        return noise;
    };

    auto removeMagicRuns = [](std::vector<uint8_t> &noise)
    {
        const uint8_t *magicBytes = reinterpret_cast<const uint8_t *>(&MAGIC);
        if (noise.size() < sizeof(uint32_t))
            return;

        for (std::size_t i = 0; i <= noise.size() - sizeof(uint32_t); ++i)
        {
            if (noise[i] == magicBytes[0] &&
                noise[i + 1] == magicBytes[1] &&
                noise[i + 2] == magicBytes[2] &&
                noise[i + 3] == magicBytes[3])
            {
                noise[i] ^= 0x5A;
            }
        }
    };

    const std::vector<uint8_t> payload1{0x01, 0x02, 0x03};
    const std::vector<uint8_t> payload2{0x11, 0x12, 0x13, 0x14};
    const std::vector<uint8_t> payload3{0x21, 0x22};

    const auto packet1 = makePacket(660, 66'000ULL, payload1);
    const auto packet2 = makePacket(661, 66'001ULL, payload2);
    const auto packet3 = makePacket(662, 66'002ULL, payload3);

    auto noise1 = makeNoise(17, 0x12345678U);
    auto noise2 = makeNoise(29, 0xCAFEBABEU);
    removeMagicRuns(noise1);
    removeMagicRuns(noise2);

    RingBuffer rb(packet1.size() + noise1.size() + packet2.size() + noise2.size() + packet3.size());
    REQUIRE(rb.write(packet1));
    REQUIRE(rb.write(noise1));
    REQUIRE(rb.write(packet2));
    REQUIRE(rb.write(noise2));
    REQUIRE(rb.write(packet3));

    ParseState state = ParseState::SeekMagic;
    const std::array<uint32_t, 3> expectedSequences{660, 661, 662};
    const std::array<std::vector<uint8_t>, 3> expectedPayloads{payload1, payload2, payload3};

    PacketHeader workingHeader{};
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    ProtocolStats stats{};

    std::size_t parsedCount = 0;
    std::size_t attempts = 0;
    const std::size_t maxAttempts = rb.size() * 2;

    while (parsedCount < expectedSequences.size() && attempts < maxAttempts)
    {
        const size_t before = rb.size();
        if (usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats))
        {
            REQUIRE(rb.size() < before);
            REQUIRE(parsedHeader.sequence == expectedSequences[parsedCount]);
            REQUIRE(parsedPayload == expectedPayloads[parsedCount]);
            parsedCount++;
        }
        attempts++;
    }

    REQUIRE(parsedCount == expectedSequences.size());
    REQUIRE(rb.size() == 0);
}

TEST_CASE("tryParsePacket accounts for valid packets and payload bytes", "[protocol][parser]")
{
    // Arrange
    const std::vector<uint8_t> payload1{0x01, 0x02, 0x03, 0x04};
    const std::vector<uint8_t> payload2{0xA0, 0xB0, 0xC0};
    const auto packet1 = makePacket(700, 70'000ULL, payload1);
    const auto packet2 = makePacket(701, 70'001ULL, payload2);

    RingBuffer rb(packet1.size() + packet2.size());
    REQUIRE(rb.write(packet1));
    REQUIRE(rb.write(packet2));

    ParseState state = ParseState::SeekMagic;
    PacketHeader workingHeader{};
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    ProtocolStats stats{};

    // Act
    REQUIRE(usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats));

    // Assert
    REQUIRE(parsedHeader.sequence == 700);
    REQUIRE(parsedPayload == payload1);
    REQUIRE(state == ParseState::SeekMagic);
    REQUIRE(stats.parsedPackets == 1U);
    REQUIRE(stats.validPackets == 1U);
    REQUIRE(stats.totalPayloadBytes == payload1.size());
    REQUIRE(stats.invalidPackets == 0U);
    REQUIRE(stats.crcFailures == 0U);
    REQUIRE(stats.malformedHeaders == 0U);
    REQUIRE(stats.discardedBytes == 0U);

    // Act
    REQUIRE(usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats));

    // Assert
    REQUIRE(parsedHeader.sequence == 701);
    REQUIRE(parsedPayload == payload2);
    REQUIRE(state == ParseState::SeekMagic);
    REQUIRE(rb.size() == 0);
    REQUIRE(stats.parsedPackets == 2U);
    REQUIRE(stats.validPackets == 2U);
    REQUIRE(stats.totalPayloadBytes == payload1.size() + payload2.size());
    REQUIRE(stats.invalidPackets == 0U);
    REQUIRE(stats.crcFailures == 0U);
    REQUIRE(stats.malformedHeaders == 0U);
    REQUIRE(stats.discardedBytes == 0U);
}

TEST_CASE("tryParsePacket accounts for CRC failures and resynchronizes afterward", "[protocol][parser]")
{
    // Arrange
    const std::vector<uint8_t> badPayload{0x10, 0x11, 0x12, 0x13};
    const std::vector<uint8_t> goodPayload{0x20, 0x21, 0x22};
    const auto corruptedPacket = makePayloadCorruptedPacket(710, 71'000ULL, badPayload, 0x5A);
    const auto validPacket = makePacket(711, 71'001ULL, goodPayload);

    RingBuffer rb(corruptedPacket.size() + validPacket.size());
    REQUIRE(rb.write(corruptedPacket));
    REQUIRE(rb.write(validPacket));

    ParseState state = ParseState::SeekMagic;
    PacketHeader workingHeader{};
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    ProtocolStats stats{};

    // Act
    const std::size_t initialSize = rb.size();
    const bool parsedCorruptedPacket = usblink::protocol::tryParsePacket(
        rb, state, workingHeader, parsedHeader, parsedPayload, stats);

    // Assert
    REQUIRE_FALSE(parsedCorruptedPacket);
    REQUIRE(state == ParseState::SeekMagic);
    REQUIRE(rb.size() == initialSize - 1U);
    REQUIRE(stats.parsedPackets == 0U);
    REQUIRE(stats.validPackets == 0U);
    REQUIRE(stats.totalPayloadBytes == 0U);
    REQUIRE(stats.crcFailures == 1U);
    REQUIRE(stats.invalidPackets == 1U);
    REQUIRE(stats.discardedBytes == 1U);

    // Act
    const bool recovered = usblink::protocol::tryParsePacket(
        rb, state, workingHeader, parsedHeader, parsedPayload, stats);

    // Assert
    REQUIRE(recovered);
    REQUIRE(parsedHeader.sequence == 711);
    REQUIRE(parsedPayload == goodPayload);
    REQUIRE(state == ParseState::SeekMagic);
    REQUIRE(rb.size() == 0);
    REQUIRE(stats.parsedPackets == 1U);
    REQUIRE(stats.validPackets == 1U);
    REQUIRE(stats.totalPayloadBytes == goodPayload.size());
    REQUIRE(stats.crcFailures == 1U);
    REQUIRE(stats.invalidPackets == 1U);
    REQUIRE(stats.resyncEvents == 1U);
    REQUIRE(stats.discardedBytes == corruptedPacket.size());
}

TEST_CASE("tryParsePacket accounts for malformed headers", "[protocol][parser]")
{
    // Arrange
    const std::vector<uint8_t> emptyPayload;
    const auto malformedPacket = makeOversizedHeaderPacket(720, 72'000ULL, emptyPayload);

    RingBuffer rb(malformedPacket.size());
    REQUIRE(rb.write(malformedPacket));

    ParseState state = ParseState::SeekMagic;
    PacketHeader workingHeader{};
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    ProtocolStats stats{};

    // Act
    const bool parsed = usblink::protocol::tryParsePacket(
        rb, state, workingHeader, parsedHeader, parsedPayload, stats);

    // Assert
    REQUIRE_FALSE(parsed);
    REQUIRE(state == ParseState::SeekMagic);
    REQUIRE(rb.size() == sizeof(uint32_t) - 1U);
    REQUIRE(stats.parsedPackets == 0U);
    REQUIRE(stats.validPackets == 0U);
    REQUIRE(stats.totalPayloadBytes == 0U);
    REQUIRE(stats.malformedHeaders == 1U);
    REQUIRE(stats.invalidPackets == 1U);
    REQUIRE(stats.crcFailures == 0U);
    REQUIRE(stats.resyncEvents == 1U);
    REQUIRE(stats.discardedBytes == malformedPacket.size() - (sizeof(uint32_t) - 1U));
}

TEST_CASE("tryParsePacket accounts for garbage-byte resynchronization before valid magic", "[protocol][parser]")
{
    // Arrange
    const std::vector<uint8_t> garbage{0x10, 0x20, 0x30, 0x40, 0x50};
    const std::vector<uint8_t> payload{0xAA, 0xBB, 0xCC};
    const auto packet = makePacket(730, 73'000ULL, payload);

    RingBuffer rb(garbage.size() + packet.size());
    REQUIRE(rb.write(garbage));
    REQUIRE(rb.write(packet));

    ParseState state = ParseState::SeekMagic;
    PacketHeader workingHeader{};
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    ProtocolStats stats{};

    // Act
    const bool parsed = usblink::protocol::tryParsePacket(
        rb, state, workingHeader, parsedHeader, parsedPayload, stats);

    // Assert
    REQUIRE(parsed);
    REQUIRE(parsedHeader.sequence == 730);
    REQUIRE(parsedPayload == payload);
    REQUIRE(state == ParseState::SeekMagic);
    REQUIRE(rb.size() == 0);
    REQUIRE(stats.parsedPackets == 1U);
    REQUIRE(stats.validPackets == 1U);
    REQUIRE(stats.totalPayloadBytes == payload.size());
    REQUIRE(stats.invalidPackets == 0U);
    REQUIRE(stats.resyncEvents == 1U);
    REQUIRE(stats.discardedBytes == garbage.size());
}

TEST_CASE("tryParsePacket preserves a partial magic suffix across fragmented reads", "[protocol][parser]")
{
    // Arrange
    const std::vector<uint8_t> payload{0x01, 0x23, 0x45, 0x67};
    const auto packet = makePacket(740, 74'000ULL, payload);
    const std::vector<uint8_t> firstChunk{0x99, packet[0], packet[1], packet[2]};

    RingBuffer rb(firstChunk.size() + packet.size());
    REQUIRE(rb.write(firstChunk));

    ParseState state = ParseState::SeekMagic;
    PacketHeader workingHeader{};
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    ProtocolStats stats{};

    // Act
    const bool parsedBeforeMagicCompleted = usblink::protocol::tryParsePacket(
        rb, state, workingHeader, parsedHeader, parsedPayload, stats);

    // Assert
    REQUIRE_FALSE(parsedBeforeMagicCompleted);
    REQUIRE(state == ParseState::SeekMagic);
    REQUIRE(rb.size() == sizeof(uint32_t) - 1U);
    REQUIRE(rb[0] == packet[0]);
    REQUIRE(rb[1] == packet[1]);
    REQUIRE(rb[2] == packet[2]);
    REQUIRE(stats.resyncEvents == 1U);
    REQUIRE(stats.discardedBytes == 1U);
    REQUIRE(stats.parsedPackets == 0U);
    REQUIRE(stats.validPackets == 0U);
    REQUIRE(stats.invalidPackets == 0U);

    // Act
    REQUIRE(rb.write(std::span<const uint8_t>(packet.data() + 3, packet.size() - 3)));
    const bool parsedAfterMagicCompleted = usblink::protocol::tryParsePacket(
        rb, state, workingHeader, parsedHeader, parsedPayload, stats);

    // Assert
    REQUIRE(parsedAfterMagicCompleted);
    REQUIRE(parsedHeader.sequence == 740);
    REQUIRE(parsedHeader.timestamp == 74'000ULL);
    REQUIRE(parsedHeader.payloadSize == payload.size());
    REQUIRE(parsedPayload == payload);
    REQUIRE(state == ParseState::SeekMagic);
    REQUIRE(rb.size() == 0);
    REQUIRE(stats.parsedPackets == 1U);
    REQUIRE(stats.validPackets == 1U);
    REQUIRE(stats.totalPayloadBytes == payload.size());
    REQUIRE(stats.invalidPackets == 0U);
    REQUIRE(stats.resyncEvents == 1U);
    REQUIRE(stats.discardedBytes == 1U);
}

TEST_CASE("tryParsePacket resets state and continues after corruption and noise", "[protocol][parser]")
{
    // Arrange
    const std::vector<uint8_t> payload1{0x01, 0x02};
    const std::vector<uint8_t> badPayload{0xAA, 0xBB, 0xCC, 0xDD};
    const std::vector<uint8_t> noise{0x12, 0x34, 0x56, 0x78, 0x9A};
    const std::vector<uint8_t> payload2{0x03, 0x04, 0x05};

    const auto packet1 = makePacket(750, 75'000ULL, payload1);
    const auto corruptedPacket = makePayloadCorruptedPacket(751, 75'001ULL, badPayload, 0xA5);
    const auto packet2 = makePacket(752, 75'002ULL, payload2);

    RingBuffer rb(packet1.size() + corruptedPacket.size() + noise.size() + packet2.size());
    REQUIRE(rb.write(packet1));
    REQUIRE(rb.write(corruptedPacket));
    REQUIRE(rb.write(noise));
    REQUIRE(rb.write(packet2));

    ParseState state = ParseState::SeekMagic;
    PacketHeader workingHeader{};
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    ProtocolStats stats{};

    // Act
    REQUIRE(usblink::protocol::tryParsePacket(rb, state, workingHeader, parsedHeader, parsedPayload, stats));

    // Assert
    REQUIRE(parsedHeader.sequence == 750);
    REQUIRE(parsedPayload == payload1);
    REQUIRE(state == ParseState::SeekMagic);
    REQUIRE(stats.parsedPackets == 1U);
    REQUIRE(stats.validPackets == 1U);
    REQUIRE(stats.totalPayloadBytes == payload1.size());

    // Act
    const bool parsedCorruptedPacket = usblink::protocol::tryParsePacket(
        rb, state, workingHeader, parsedHeader, parsedPayload, stats);

    // Assert
    REQUIRE_FALSE(parsedCorruptedPacket);
    REQUIRE(state == ParseState::SeekMagic);
    REQUIRE(stats.crcFailures == 1U);
    REQUIRE(stats.invalidPackets == 1U);
    REQUIRE(stats.discardedBytes == 1U);

    // Act
    const bool recovered = usblink::protocol::tryParsePacket(
        rb, state, workingHeader, parsedHeader, parsedPayload, stats);

    // Assert
    REQUIRE(recovered);
    REQUIRE(parsedHeader.sequence == 752);
    REQUIRE(parsedPayload == payload2);
    REQUIRE(state == ParseState::SeekMagic);
    REQUIRE(rb.size() == 0);
    REQUIRE(stats.parsedPackets == 2U);
    REQUIRE(stats.validPackets == 2U);
    REQUIRE(stats.totalPayloadBytes == payload1.size() + payload2.size());
    REQUIRE(stats.crcFailures == 1U);
    REQUIRE(stats.invalidPackets == 1U);
    REQUIRE(stats.resyncEvents == 1U);
    REQUIRE(stats.discardedBytes == corruptedPacket.size() + noise.size());
}
