#include "usblink/transport/posix/PosixSerialTransport.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <cstring>

namespace usblink::transport
{
    static speed_t toBaud(uint32_t baud)
    {
        switch (baud)
        {
        case 9600:
            return B9600;
        case 19200:
            return B19200;
        case 38400:
            return B38400;
        case 115200:
            return B115200;
        default:
            return B9600;
        }
    }

    TransportResult PosixSerialTransport::open(const SerialConfig &cfg)
    {
        if (isOpen())
            return {TransportStatus::AlreadyOpen};

        fd_ = -1;

        fd_ = ::open(cfg.portName.c_str(), O_RDWR | O_NOCTTY);

        if (fd_ < 0)
        {
            if (errno == ENOENT)
                return {TransportStatus::PortNotFound};

            if (errno == EACCES)
                return {TransportStatus::AccessDenied};

            return {TransportStatus::IoError};
        }

        struct termios tty{};

        if (tcgetattr(fd_, &tty) != 0)
        {
            ::close(fd_);
            fd_ = -1;
            return {TransportStatus::IoError};
        }

        cfmakeraw(&tty); // Disable all OS-level processing, enable raw byte stream

        auto speed = toBaud(cfg.baudRate);
        cfsetispeed(&tty, speed); // set input rate
        cfsetospeed(&tty, speed); // set output rate

        tty.c_cflag |= (CLOCAL | CREAD); // Enable receiver and ignore modem control signals

        if (tcsetattr(fd_, TCSANOW, &tty) != 0)
        {
            ::close(fd_);
            fd_ = -1;
            return {TransportStatus::IoError};
        }

        config_ = cfg;
        isOpen_ = true;

        return {TransportStatus::Ok};
    }

    void PosixSerialTransport::close()
    {
        if (fd_ >= 0)
        {
            ::close(fd_);
            fd_ = -1;
            isOpen_ = false;
        }
    }

    PosixSerialTransport::~PosixSerialTransport()
    {
        close();
    }

    bool PosixSerialTransport::isOpen() const
    {
        return isOpen_;
    }

} // namespace usblink::transport
