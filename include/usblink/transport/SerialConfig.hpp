#pragma once

#include <string>
#include <chrono>

namespace usblink::transport
{

    enum class DataBits
    {
        Five = 5,
        Six = 6,
        Seven = 7,
        Eight = 8
    };

    enum class Parity
    {
        None,
        Even,
        Odd
    };

    enum class StopBits
    {
        One,
        Two
    };

    enum class IoMode
    {
        Blocking,
        NonBlocking
    };

    struct SerialConfig
    {

        // Serial device path (e.g., /dev/tty.usbserial-XXXX on macOS)
        std::string portName;

        // Communication speed (baud rate)
        std::uint32_t baudRate = 9600;

        // Frame format (default: 8N1)
        DataBits dataBits = DataBits::Eight;
        Parity parity = Parity::None;
        StopBits stopBits = StopBits::One;

        // I/O behavior
        IoMode ioMode = IoMode::Blocking;

        // Read timeout duration
        std::chrono::milliseconds readTimeout{1000};
    };

} // namespace usblink::transport