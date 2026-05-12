#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace usblink::protocol
{
    enum class PayloadPattern
    {
        Fixed,
        Incremental,
        Random
    };

    class PayloadGenerator
    {
    public:
        static std::vector<uint8_t> generateFixed(std::size_t size, uint8_t value);

        static std::vector<uint8_t> generateIncremental(std::size_t size);

        static std::vector<uint8_t> generateRandom(std::size_t size, uint32_t seed);
    };
} // namespace usblink::protocol