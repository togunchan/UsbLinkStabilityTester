#pragma once

#include <cstddef>
#include <vector>
#include <span>

namespace usblink::core
{
    class RingBuffer
    {
    public:
        explicit RingBuffer(const size_t capacity);

        bool write(std::span<const uint8_t> data);
        void consume(size_t count);

        uint8_t operator[](size_t index) const;

        size_t size() const;
        size_t capacity() const;
        bool empty() const;

    private:
        size_t head_{0};
        size_t tail_{0};
        size_t size_{0};
        std::vector<std::uint8_t> buffer_{0};
    };
} // namespace usblink::core