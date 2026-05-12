#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#elif __has_include(<catch2/catch.hpp>)
#include <catch2/catch.hpp>
#else
#error "Catch2 headers not found"
#endif

#include "usblink/protocol/PayloadGenerator.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

TEST_CASE("generateFixed creates payload with requested size and value", "[protocol][payload]")
{
    const std::size_t size = 16;
    const uint8_t value = 0xAB;

    const std::vector<uint8_t> payload = usblink::protocol::PayloadGenerator::generateFixed(size, value);

    REQUIRE(payload.size() == size);
    REQUIRE(std::all_of(payload.begin(), payload.end(), [value](uint8_t byte) {
        return byte == value;
    }));
}

TEST_CASE("generateFixed supports zero-length payload", "[protocol][payload]")
{
    const std::vector<uint8_t> payload = usblink::protocol::PayloadGenerator::generateFixed(0, 0xAB);

    REQUIRE(payload.empty());
}

TEST_CASE("generateIncremental creates sequential byte pattern", "[protocol][payload]")
{
    const std::size_t size = 16;

    const std::vector<uint8_t> payload = usblink::protocol::PayloadGenerator::generateIncremental(size);

    REQUIRE(payload.size() == size);
    for (std::size_t i = 0; i < payload.size(); ++i)
    {
        REQUIRE(payload[i] == i);
    }
}

TEST_CASE("generateIncremental supports zero-length payload", "[protocol][payload]")
{
    const std::vector<uint8_t> payload = usblink::protocol::PayloadGenerator::generateIncremental(0);

    REQUIRE(payload.empty());
}

TEST_CASE("generateRandom produces deterministic output for same seed", "[protocol][payload]")
{
    const std::size_t size = 16;
    const uint32_t seed = 12345;

    const std::vector<uint8_t> first = usblink::protocol::PayloadGenerator::generateRandom(size, seed);
    const std::vector<uint8_t> second = usblink::protocol::PayloadGenerator::generateRandom(size, seed);

    REQUIRE(first == second);
}

TEST_CASE("generateRandom produces different output for different seeds", "[protocol][payload]")
{
    const std::size_t size = 16;

    const std::vector<uint8_t> first = usblink::protocol::PayloadGenerator::generateRandom(size, 12345);
    const std::vector<uint8_t> second = usblink::protocol::PayloadGenerator::generateRandom(size, 54321);

    REQUIRE(first != second);
}

TEST_CASE("generateRandom supports zero-length payload", "[protocol][payload]")
{
    const std::vector<uint8_t> payload = usblink::protocol::PayloadGenerator::generateRandom(0, 12345);

    REQUIRE(payload.empty());
}
