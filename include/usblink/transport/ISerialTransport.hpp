#pragma once

#include <cstdint>
#include <span>

#include "usblink/transport/SerialConfig.hpp"
#include "usblink/transport/SerialTypes.hpp"

namespace usblink::transport
{
    class ISerialTransport{
    public:
        virtual ~ISerialTransport() = default;

        virtual TransportResult open(const SerialConfig &cfg) = 0;
        virtual void close() = 0;
        virtual bool isOpen() const = 0;

        virtual ReadResult read(std::span<std::uint8_t> buffer) = 0;
        virtual WriteResult write(std::span<const std::uint8_t> data) = 0;
    };
} // namespace usblink::transport
