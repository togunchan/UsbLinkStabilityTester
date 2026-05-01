#include "usblink/core/RingBuffer.hpp"

namespace usblink::core
{
    RingBuffer::RingBuffer(const size_t capacity) : buffer_(capacity), head_(0), tail_(0), size_(0) {}

    bool RingBuffer::write(std::span<const uint8_t> data)
    {
        if (data.size() > buffer_.size() - size_)
            return false;

        for(uint8_t byte : data){
            buffer_[tail_] = byte;
            tail_ = (tail_ + 1) % buffer_.size();
            size_++;
        }

        return true;
    }

    void RingBuffer::consume(size_t count){
        if(count > size_)
            count = size_;

        head_ = (head_ + count) % buffer_.size();
        size_ -= count;
    }

    uint8_t RingBuffer::operator[](size_t index) const{
        return buffer_[(head_ + index) % buffer_.size()];
    }

    size_t RingBuffer::size() const
    {
        return size_;
    }

    size_t RingBuffer::capacity() const
    {
        return buffer_.size();
    }

    bool RingBuffer::empty() const
    {
        return size_ == 0;
    }

} // namespace usblink::core
