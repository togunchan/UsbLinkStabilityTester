#pragma once

#pragma once

#include <cstdint>
#include <span>

#include "usblink/transport/ISerialTransport.hpp"

namespace usblink::transport
{
    class PosixSerialTransport : public ISerialTransport{
        public:
            PosixSerialTransport() = default;
            ~PosixSerialTransport() override;

            // Non-copyable
            PosixSerialTransport(const PosixSerialTransport &) = delete;
            PosixSerialTransport &operator=(const PosixSerialTransport &) = delete;

            // // Non-movable
            PosixSerialTransport(PosixSerialTransport &&) = delete;
            PosixSerialTransport &operator=(PosixSerialTransport &&) = delete;

            TransportResult open(const SerialConfig &config) override;
            void close() override;
            bool isOpen() const override;

            ReadResult read(std::span<std::uint8_t> buffer) override;
            WriteResult write(std::span<const std::uint8_t> data) override;

        private:
            int fd_ = -1;
            bool isOpen_ = false;
            SerialConfig config_;
    };
} // namespace usblink::transport
