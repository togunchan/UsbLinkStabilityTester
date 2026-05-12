#include "usblink/protocol/PayloadGenerator.hpp"
#include <random>

namespace usblink::protocol
{
    std::vector<uint8_t> PayloadGenerator::generateFixed(std::size_t size, uint8_t value)
    {
        return std::vector<uint8_t>(size, value);
    }

    std::vector<uint8_t> PayloadGenerator::generateIncremental(std::size_t size)
    {
        std::vector<uint8_t> payload(size);
        for (size_t i = 0; i < size; ++i)
        {
            payload[i] = static_cast<uint8_t>(i);
        }
        return payload;
    }

    std::vector<uint8_t> PayloadGenerator::generateRandom(std::size_t size, uint32_t seed)
    {
        std::mt19937 rng(seed);
        std::uniform_int_distribution<int> dist(0, 255);

        std::vector<uint8_t> payload(size);
        for (size_t i = 0; i < size; ++i)
        {
            payload[i] = static_cast<uint8_t>(dist(rng));
        }

        return payload;
    }

} // namespace usblink::protocol
