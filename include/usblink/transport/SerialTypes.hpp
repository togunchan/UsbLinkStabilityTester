#pragma once

#include <cstddef>
#include <string>

namespace usblink::transport{
    enum class TransportStatus
    {
        Ok,
        NotOpen,
        AlreadyOpen,
        PortNotFound,
        AccessDenied,
        Timeout,
        IoError,
        InvalidArgument,
        UnknownError
    };

    struct TransportResult{
        TransportStatus status;

        bool isOk() const
        {
            return status == TransportStatus::Ok;
        }
    };

    struct ReadResult
    {
        TransportStatus status;
        std::size_t bytesRead;
    };

    struct WriteResult
    {
        TransportStatus status;
        std::size_t bytesWritten;
    };
} // namespace usblink::transport