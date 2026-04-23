#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#elif __has_include(<catch2/catch.hpp>)
#include <catch2/catch.hpp>
#else
#error "Catch2 headers not found"
#endif

#include "usblink/protocol/PacketEncoder.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace
{
    using usblink::protocol::MAGIC;
    using usblink::protocol::MAX_PAYLOAD_SIZE;
    using usblink::protocol::PacketHeader;

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

    void appendBytes(std::vector<uint8_t> &dst, std::span<const uint8_t> src)
    {
        dst.insert(dst.end(), src.begin(), src.end());
    }
} // namespace

TEST_CASE("tryParsePacket parses a complete valid packet", "[protocol][parser]")
{
    const std::vector<uint8_t> payload{0x10, 0x20, 0x30, 0x40, 0x50};
    const auto encoded = makePacket(42, 1'700'000'000ULL, payload);

    std::vector<uint8_t> buffer = encoded;
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    size_t cursor = 0;

    REQUIRE(usblink::protocol::tryParsePacket(buffer, cursor, parsedHeader, parsedPayload));
    REQUIRE(parsedHeader.magic == MAGIC);
    REQUIRE(parsedHeader.sequence == 42);
    REQUIRE(parsedHeader.timestamp == 1'700'000'000ULL);
    REQUIRE(parsedHeader.payloadSize == payload.size());
    REQUIRE(parsedPayload == payload);
    REQUIRE(cursor == buffer.size());
}

TEST_CASE("tryParsePacket waits when only a partial header is available", "[protocol][parser]")
{
    const std::vector<uint8_t> payload{0x01, 0x02, 0x03};
    const auto encoded = makePacket(7, 777ULL, payload);

    const std::size_t partialHeaderSize = sizeof(PacketHeader) - 3;
    REQUIRE(partialHeaderSize < sizeof(PacketHeader));

    std::vector<uint8_t> buffer;
    appendBytes(buffer, std::span<const uint8_t>(encoded.data(), partialHeaderSize));

    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    size_t cursor = 0;

    REQUIRE_FALSE(usblink::protocol::tryParsePacket(buffer, cursor, parsedHeader, parsedPayload));
    REQUIRE(cursor == 0);
    REQUIRE(buffer.size() == partialHeaderSize);
}

TEST_CASE("tryParsePacket waits for remaining payload bytes", "[protocol][parser]")
{
    const std::vector<uint8_t> payload{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xF0};
    const auto encoded = makePacket(8, 8'888ULL, payload);

    const std::size_t incompleteSize = sizeof(PacketHeader) + payload.size() - 2;

    std::vector<uint8_t> buffer;
    appendBytes(buffer, std::span<const uint8_t>(encoded.data(), incompleteSize));

    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    size_t cursor = 0;

    REQUIRE_FALSE(usblink::protocol::tryParsePacket(buffer, cursor, parsedHeader, parsedPayload));
    REQUIRE(cursor == 0);
    REQUIRE(buffer.size() == incompleteSize);

    appendBytes(
        buffer,
        std::span<const uint8_t>(
            encoded.data() + incompleteSize,
            encoded.size() - incompleteSize));

    REQUIRE(usblink::protocol::tryParsePacket(buffer, cursor, parsedHeader, parsedPayload));
    REQUIRE(parsedHeader.sequence == 8);
    REQUIRE(parsedHeader.timestamp == 8'888ULL);
    REQUIRE(parsedHeader.payloadSize == payload.size());
    REQUIRE(parsedPayload == payload);
    REQUIRE(cursor == buffer.size());
}

TEST_CASE("tryParsePacket skips garbage bytes before a valid packet", "[protocol][parser]")
{
    const std::vector<uint8_t> payload{0x44, 0x55, 0x66};
    const auto encoded = makePacket(9, 9'999ULL, payload);

    std::vector<uint8_t> buffer{0x13, 0x37, 0xBA, 0xAD, 0xF0, 0x0D};

    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    size_t cursor = 0;

    REQUIRE_FALSE(usblink::protocol::tryParsePacket(buffer, cursor, parsedHeader, parsedPayload));
    REQUIRE(cursor == 0);

    appendBytes(buffer, encoded);

    REQUIRE(usblink::protocol::tryParsePacket(buffer, cursor, parsedHeader, parsedPayload));
    REQUIRE(parsedHeader.sequence == 9);
    REQUIRE(parsedPayload == payload);
    REQUIRE(cursor == buffer.size());
}

TEST_CASE("tryParsePacket rejects CRC-mismatched packet and resynchronizes", "[protocol][parser]")
{
    const std::vector<uint8_t> badPayload{0x01, 0x03, 0x05, 0x07, 0x09};
    auto corruptedPacket = makePacket(10, 10'101ULL, badPayload);
    corruptedPacket[sizeof(PacketHeader) + 1] ^= 0xFF; // break CRC

    const std::vector<uint8_t> goodPayload{0xDE, 0xAD, 0xBE, 0xEF};
    const auto validPacket = makePacket(11, 11'111ULL, goodPayload);

    std::vector<uint8_t> buffer;
    appendBytes(buffer, corruptedPacket);
    appendBytes(buffer, validPacket);

    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    size_t cursor = 0;

    const std::size_t initialSize = buffer.size();
    REQUIRE_FALSE(usblink::protocol::tryParsePacket(buffer, cursor, parsedHeader, parsedPayload));
    REQUIRE(cursor == 1); // one-byte drop resync policy

    bool recovered = false;
    std::size_t attempts = 0;
    const std::size_t maxAttempts = initialSize * 2;

    while (!recovered && attempts < maxAttempts)
    {
        recovered = usblink::protocol::tryParsePacket(buffer, cursor, parsedHeader, parsedPayload);
        attempts++;
    }

    REQUIRE(recovered);
    REQUIRE(parsedHeader.sequence == 11);
    REQUIRE(parsedHeader.timestamp == 11'111ULL);
    REQUIRE(parsedHeader.payloadSize == goodPayload.size());
    REQUIRE(parsedPayload == goodPayload);
    REQUIRE(cursor == buffer.size());
}

TEST_CASE("tryParsePacket returns at most one packet per call", "[protocol][parser]")
{
    const std::vector<uint8_t> payload1{0x01, 0x02};
    const std::vector<uint8_t> payload2{0xA0, 0xB0, 0xC0};

    const auto packet1 = makePacket(100, 1000ULL, payload1);
    const auto packet2 = makePacket(101, 1001ULL, payload2);

    std::vector<uint8_t> buffer;
    appendBytes(buffer, packet1);
    appendBytes(buffer, packet2);

    size_t cursor = 0;
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;

    const std::array<uint32_t, 2> expectedSequences{100, 101};
    const std::array<std::vector<uint8_t>, 2> expectedPayloads{payload1, payload2};

    for (std::size_t i = 0; i < expectedSequences.size(); ++i)
    {
        const size_t before = cursor;
        REQUIRE(usblink::protocol::tryParsePacket(buffer, cursor, parsedHeader, parsedPayload));
        REQUIRE(cursor > before);
        REQUIRE(parsedHeader.sequence == expectedSequences[i]);
        REQUIRE(parsedPayload == expectedPayloads[i]);
        if (i == 0)
            REQUIRE(cursor < buffer.size());
    }

    REQUIRE(cursor == buffer.size());
}

TEST_CASE("tryParsePacket handles stream-split input and succeeds only when complete", "[protocol][parser]")
{
    const std::vector<uint8_t> payload{0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    const auto encoded = makePacket(555, 55'555ULL, payload);

    const std::array<std::size_t, 5> chunkSizes{1, 4, 3, 2, 64};

    std::vector<uint8_t> buffer;
    size_t cursor = 0;
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;

    std::size_t consumed = 0;

    for (std::size_t chunkSize : chunkSizes)
    {
        if (consumed >= encoded.size())
            break;

        const std::size_t bytesToCopy = std::min(chunkSize, encoded.size() - consumed);
        appendBytes(
            buffer,
            std::span<const uint8_t>(
                encoded.data() + consumed,
                bytesToCopy));
        consumed += bytesToCopy;

        const bool parsed = usblink::protocol::tryParsePacket(buffer, cursor, parsedHeader, parsedPayload);

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
    REQUIRE(cursor == buffer.size());
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

    std::vector<uint8_t> buffer;
    const std::size_t splitPoint = sizeof(PacketHeader) + 6;
    appendBytes(buffer, std::span<const uint8_t>(firstPacket.data(), splitPoint));

    size_t cursor = 0;
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;

    REQUIRE_FALSE(usblink::protocol::tryParsePacket(buffer, cursor, parsedHeader, parsedPayload));
    REQUIRE(cursor == 0);

    appendBytes(
        buffer,
        std::span<const uint8_t>(firstPacket.data() + splitPoint, firstPacket.size() - splitPoint));
    appendBytes(buffer, secondPacket);

    const std::array<uint32_t, 2> expectedSequences{600, 601};
    const std::array<std::vector<uint8_t>, 2> expectedPayloads{payloadWithMagic, trailingPayload};

    for (std::size_t i = 0; i < expectedSequences.size(); ++i)
    {
        const size_t before = cursor;
        REQUIRE(usblink::protocol::tryParsePacket(buffer, cursor, parsedHeader, parsedPayload));
        REQUIRE(cursor > before);
        REQUIRE(parsedHeader.sequence == expectedSequences[i]);
        REQUIRE(parsedPayload == expectedPayloads[i]);
    }

    REQUIRE(cursor == buffer.size());
}

TEST_CASE("tryParsePacket parses zero-length payload packets", "[protocol][parser]")
{
    const std::vector<uint8_t> emptyPayload;
    const auto encoded = makePacket(610, 61'000ULL, emptyPayload);

    REQUIRE(encoded.size() == sizeof(PacketHeader));

    std::vector<uint8_t> buffer = encoded;
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload{0xFF}; // ensure parser overwrites output state
    size_t cursor = 0;

    REQUIRE(usblink::protocol::tryParsePacket(buffer, cursor, parsedHeader, parsedPayload));
    REQUIRE(parsedHeader.sequence == 610);
    REQUIRE(parsedHeader.payloadSize == 0);
    REQUIRE(parsedPayload.empty());
    REQUIRE(cursor == buffer.size());
}

TEST_CASE("tryParsePacket accepts payload at MAX_PAYLOAD_SIZE boundary", "[protocol][parser]")
{
    std::vector<uint8_t> maxPayload(MAX_PAYLOAD_SIZE);
    for (std::size_t i = 0; i < maxPayload.size(); ++i)
        maxPayload[i] = static_cast<uint8_t>((i * 13U + 7U) & 0xFFU);

    const auto encoded = makePacket(615, 61'500ULL, maxPayload);

    std::vector<uint8_t> buffer = encoded;
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;
    size_t cursor = 0;

    REQUIRE(usblink::protocol::tryParsePacket(buffer, cursor, parsedHeader, parsedPayload));
    REQUIRE(parsedHeader.sequence == 615);
    REQUIRE(parsedHeader.payloadSize == MAX_PAYLOAD_SIZE);
    REQUIRE(parsedPayload == maxPayload);
    REQUIRE(cursor == buffer.size());
}

TEST_CASE("tryParsePacket rejects payloadSize above MAX_PAYLOAD_SIZE and resynchronizes", "[protocol][parser]")
{
    const std::vector<uint8_t> basePayload{0xAA, 0xBB, 0xCC, 0xDD};
    auto oversizedHeaderPacket = makePacket(616, 61'600ULL, basePayload);

    constexpr std::size_t payloadSizeOffset = sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint64_t);
    const uint32_t oversized = static_cast<uint32_t>(MAX_PAYLOAD_SIZE + 1);
    const uint8_t *oversizedBytes = reinterpret_cast<const uint8_t *>(&oversized);
    for (std::size_t i = 0; i < sizeof(uint32_t); ++i)
        oversizedHeaderPacket[payloadSizeOffset + i] = oversizedBytes[i];

    const std::vector<uint8_t> validPayload{0x01, 0x03, 0x05};
    const auto validPacket = makePacket(617, 61'700ULL, validPayload);

    std::vector<uint8_t> buffer;
    appendBytes(buffer, oversizedHeaderPacket);
    appendBytes(buffer, validPacket);

    size_t cursor = 0;
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;

    REQUIRE_FALSE(usblink::protocol::tryParsePacket(buffer, cursor, parsedHeader, parsedPayload));
    REQUIRE(cursor == 1);

    bool recovered = false;
    std::size_t attempts = 0;
    const std::size_t maxAttempts = buffer.size() * 2;
    while (!recovered && attempts < maxAttempts)
    {
        recovered = usblink::protocol::tryParsePacket(buffer, cursor, parsedHeader, parsedPayload);
        attempts++;
    }

    REQUIRE(recovered);
    REQUIRE(parsedHeader.sequence == 617);
    REQUIRE(parsedHeader.payloadSize == validPayload.size());
    REQUIRE(parsedPayload == validPayload);
    REQUIRE(cursor == buffer.size());
}

TEST_CASE("tryParsePacket correctly parses a large payload stream", "[protocol][parser]")
{
    std::vector<uint8_t> largePayload(2048);
    for (std::size_t i = 0; i < largePayload.size(); ++i)
        largePayload[i] = static_cast<uint8_t>((i * 37U + 11U) & 0xFFU);

    const auto encoded = makePacket(620, 62'000ULL, largePayload);

    std::vector<uint8_t> buffer;
    size_t cursor = 0;
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;

    std::size_t consumed = 0;
    constexpr std::size_t chunkSize = 113;

    while (consumed < encoded.size())
    {
        const std::size_t bytesToCopy = std::min(chunkSize, encoded.size() - consumed);
        appendBytes(
            buffer,
            std::span<const uint8_t>(encoded.data() + consumed, bytesToCopy));
        consumed += bytesToCopy;

        const bool parsed = usblink::protocol::tryParsePacket(buffer, cursor, parsedHeader, parsedPayload);
        if (consumed < encoded.size())
            REQUIRE_FALSE(parsed);
        else
            REQUIRE(parsed);
    }

    REQUIRE(parsedHeader.sequence == 620);
    REQUIRE(parsedHeader.payloadSize == largePayload.size());
    REQUIRE(parsedPayload == largePayload);
    REQUIRE(cursor == buffer.size());
}

TEST_CASE("tryParsePacket recovers after multiple consecutive CRC failures", "[protocol][parser]")
{
    auto makeCorruptedPacket = [](uint32_t sequence, uint64_t timestamp, std::span<const uint8_t> payload, uint8_t mask)
    {
        std::vector<uint8_t> packet = makePacket(sequence, timestamp, payload);
        packet[sizeof(PacketHeader)] ^= mask;
        return packet;
    };

    const std::vector<uint8_t> badPayload1{0x01, 0x02, 0x03, 0x04};
    const std::vector<uint8_t> badPayload2{0x10, 0x11, 0x12, 0x13, 0x14};
    const std::vector<uint8_t> badPayload3{0x21, 0x22, 0x23, 0x24, 0x25, 0x26};
    const std::vector<uint8_t> goodPayload{0xDE, 0xAD, 0xFA, 0xCE};

    const auto badPacket1 = makeCorruptedPacket(630, 63'000ULL, badPayload1, 0x11);
    const auto badPacket2 = makeCorruptedPacket(631, 63'001ULL, badPayload2, 0x22);
    const auto badPacket3 = makeCorruptedPacket(632, 63'002ULL, badPayload3, 0x33);
    const auto goodPacket = makePacket(633, 63'003ULL, goodPayload);

    std::vector<uint8_t> buffer;
    appendBytes(buffer, badPacket1);
    appendBytes(buffer, badPacket2);
    appendBytes(buffer, badPacket3);
    appendBytes(buffer, goodPacket);

    size_t cursor = 0;
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;

    REQUIRE_FALSE(usblink::protocol::tryParsePacket(buffer, cursor, parsedHeader, parsedPayload));
    REQUIRE(cursor == 1);

    bool recovered = false;
    std::size_t failedAttempts = 1;
    std::size_t attempts = 1;
    const std::size_t maxAttempts = buffer.size() * 2;

    while (!recovered && attempts < maxAttempts)
    {
        recovered = usblink::protocol::tryParsePacket(buffer, cursor, parsedHeader, parsedPayload);
        if (!recovered)
            failedAttempts++;
        attempts++;
    }

    REQUIRE(recovered);
    REQUIRE(failedAttempts >= 3);
    REQUIRE(parsedHeader.sequence == 633);
    REQUIRE(parsedPayload == goodPayload);
    REQUIRE(cursor == buffer.size());
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

    std::vector<uint8_t> buffer;
    appendBytes(buffer, badMagicPacket);
    appendBytes(buffer, badSizePacket);
    appendBytes(buffer, validPacket);

    size_t cursor = 0;
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;

    bool parsed = false;
    std::size_t attempts = 0;
    const std::size_t maxAttempts = buffer.size() * 2;
    while (!parsed && attempts < maxAttempts)
    {
        parsed = usblink::protocol::tryParsePacket(buffer, cursor, parsedHeader, parsedPayload);
        attempts++;
    }

    REQUIRE(parsed);
    REQUIRE(parsedHeader.sequence == 642);
    REQUIRE(parsedPayload == payloadC);
    REQUIRE(cursor == buffer.size());
}

TEST_CASE("tryParsePacket handles exact buffer boundary sizes without off-by-one errors", "[protocol][parser]")
{
    const std::vector<uint8_t> payload{0xAA, 0xBB, 0xCC, 0xDD};
    const auto encoded = makePacket(650, 65'000ULL, payload);

    std::vector<uint8_t> buffer;
    appendBytes(buffer, std::span<const uint8_t>(encoded.data(), sizeof(PacketHeader)));
    REQUIRE(buffer.size() == sizeof(PacketHeader));

    size_t cursor = 0;
    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;

    REQUIRE_FALSE(usblink::protocol::tryParsePacket(buffer, cursor, parsedHeader, parsedPayload));
    REQUIRE(cursor == 0);
    REQUIRE(buffer.size() == sizeof(PacketHeader));

    appendBytes(
        buffer,
        std::span<const uint8_t>(encoded.data() + sizeof(PacketHeader), payload.size()));
    REQUIRE(buffer.size() == encoded.size());

    REQUIRE(usblink::protocol::tryParsePacket(buffer, cursor, parsedHeader, parsedPayload));
    REQUIRE(parsedHeader.sequence == 650);
    REQUIRE(parsedHeader.payloadSize == payload.size());
    REQUIRE(parsedPayload == payload);
    REQUIRE(cursor == buffer.size());
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

    std::vector<uint8_t> buffer;
    appendBytes(buffer, packet1);
    appendBytes(buffer, noise1);
    appendBytes(buffer, packet2);
    appendBytes(buffer, noise2);
    appendBytes(buffer, packet3);

    size_t cursor = 0;
    const std::array<uint32_t, 3> expectedSequences{660, 661, 662};
    const std::array<std::vector<uint8_t>, 3> expectedPayloads{payload1, payload2, payload3};

    PacketHeader parsedHeader{};
    std::vector<uint8_t> parsedPayload;

    std::size_t parsedCount = 0;
    std::size_t attempts = 0;
    const std::size_t maxAttempts = buffer.size() * 2;

    while (parsedCount < expectedSequences.size() && attempts < maxAttempts)
    {
        const size_t before = cursor;
        if (usblink::protocol::tryParsePacket(buffer, cursor, parsedHeader, parsedPayload))
        {
            REQUIRE(cursor > before);
            REQUIRE(parsedHeader.sequence == expectedSequences[parsedCount]);
            REQUIRE(parsedPayload == expectedPayloads[parsedCount]);
            parsedCount++;
        }
        attempts++;
    }

    REQUIRE(parsedCount == expectedSequences.size());
    REQUIRE(cursor == buffer.size());
}
