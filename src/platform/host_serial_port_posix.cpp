#include "host_serial_port.h"

#include "host_clock.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <linux/serial.h>
#include <poll.h>
#include <sstream>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#if !defined(__linux__)
#error "host_serial_port_posix.cpp has no serial port code for this host"
#endif

namespace cerfcore {

namespace host_platform {

namespace {

struct SpeedCode {
    uint32_t rate;
    speed_t  code;
};

constexpr SpeedCode kSpeeds[] = {
    {50u, B50},           {75u, B75},           {110u, B110},         {134u, B134},
    {150u, B150},         {200u, B200},         {300u, B300},         {600u, B600},
    {1200u, B1200},       {1800u, B1800},       {2400u, B2400},       {4800u, B4800},
    {9600u, B9600},       {19200u, B19200},     {38400u, B38400},     {57600u, B57600},
    {115200u, B115200},   {230400u, B230400},   {460800u, B460800},   {500000u, B500000},
    {576000u, B576000},   {921600u, B921600},   {1000000u, B1000000}, {1152000u, B1152000},
    {1500000u, B1500000}, {2000000u, B2000000}, {2500000u, B2500000}, {3000000u, B3000000},
    {3500000u, B3500000}, {4000000u, B4000000},
};

/* QEMU chardev/char-serial.c tty_serial_init: the rate times 10/11, rounded up to the
   next standard speed, B115200 above the table. */
speed_t SpeedFor(uint32_t baud) {
    const uint64_t wanted = static_cast<uint64_t>(baud) * 10u / 11u;
    for (const SpeedCode& s : kSpeeds) {
        if (wanted <= s.rate) return s.code;
    }
    return B115200;
}

enum class WaitResult { kReady, kTimeout, kStopped, kError };

WaitResult WaitFor(int fd, short events, int stop_fd, uint32_t timeout_ms) {
    pollfd fds[2] = {{fd, events, 0}, {stop_fd, POLLIN, 0}};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        const int ready = ::poll(fds, 2, left.count() > 0 ? static_cast<int>(left.count()) : 0);
        if (ready < 0) {
            if (errno == EINTR) continue;
            return WaitResult::kError;
        }
        if ((fds[1].revents & POLLIN) != 0) return WaitResult::kStopped;
        if (fds[0].revents != 0) return WaitResult::kReady;
        if (ready == 0) return WaitResult::kTimeout;
    }
}

/* PC16550D §8.6.2 LCR bit 2 (p. 20): one stop flag gives 1.5 stop bits with a 5-bit word and
   2 with a 6-, 7- or 8-bit word; Linux serial8250_compute_lcr drives that flag from CSTOPB. */
bool StopBitsFit(SerialPortSettings::StopBits stop, uint8_t data_bits) {
    if (stop == SerialPortSettings::StopBits::kOne) return true;
    return (stop == SerialPortSettings::StopBits::kOnePointFive) == (data_bits == 5u);
}

bool PortHasNoUart(const std::string& name) {
    std::ifstream type("/sys/class/tty/" + name + "/type");
    int value = -1;
    return (type >> value) && value == PORT_UNKNOWN;
}

}

struct HostSerialPort::Impl {
    int      fd               = -1;
    int      stop             = -1;
    uint32_t read_timeout_ms  = 0;
    uint32_t write_timeout_ms = 0;
};

HostSerialPort::HostSerialPort() : impl_(std::make_unique<Impl>()) {}

HostSerialPort::~HostSerialPort() { Close(); }

bool HostSerialPort::Open(const std::string& name, uint32_t& error) {
    const std::string path = name.empty() || name[0] == '/' ? name : "/dev/" + name;
    const int fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        error = static_cast<uint32_t>(errno);
        return false;
    }
    impl_->fd = fd;
    return true;
}

void HostSerialPort::StartIo(uint32_t read_timeout_ms, uint32_t write_timeout_ms) {
    impl_->read_timeout_ms  = read_timeout_ms;
    impl_->write_timeout_ms = write_timeout_ms;
    impl_->stop             = ::eventfd(0u, EFD_CLOEXEC | EFD_NONBLOCK);
}

void HostSerialPort::Close() {
    Impl& p = *impl_;
    if (p.fd >= 0) {
        ::close(p.fd);
        p.fd = -1;
    }
    if (p.stop >= 0) {
        ::close(p.stop);
        p.stop = -1;
    }
}

bool HostSerialPort::IsOpen() const { return impl_->fd >= 0; }

SerialConfigureResult HostSerialPort::Configure(const SerialPortSettings& s, uint32_t& error) {
    termios tty{};
    if (::tcgetattr(impl_->fd, &tty) != 0) return SerialConfigureResult::kStateUnreadable;

    if (s.data_bits < 5u || s.data_bits > 8u || !StopBitsFit(s.stop, s.data_bits)) {
        error = static_cast<uint32_t>(EINVAL);
        return SerialConfigureResult::kRejected;
    }

    const speed_t speed = SpeedFor(s.baud);
    ::cfsetispeed(&tty, speed);
    ::cfsetospeed(&tty, speed);
    tty.c_iflag &= ~static_cast<tcflag_t>(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR |
                                          ICRNL | IXON | IXOFF);
    tty.c_oflag &= ~static_cast<tcflag_t>(OPOST);
    tty.c_lflag &= ~static_cast<tcflag_t>(ECHO | ECHONL | ICANON | IEXTEN | ISIG);
    tty.c_cflag &= ~static_cast<tcflag_t>(CSIZE | PARENB | PARODD | CMSPAR | CRTSCTS | CSTOPB);
    tty.c_cflag |= CREAD | CLOCAL;
    switch (s.data_bits) {
        case 5u: tty.c_cflag |= CS5; break;
        case 6u: tty.c_cflag |= CS6; break;
        case 7u: tty.c_cflag |= CS7; break;
        default: tty.c_cflag |= CS8; break;
    }
    switch (s.parity) {
        case SerialPortSettings::Parity::kNone:  break;
        case SerialPortSettings::Parity::kOdd:   tty.c_cflag |= PARENB | PARODD; break;
        case SerialPortSettings::Parity::kEven:  tty.c_cflag |= PARENB; break;
        case SerialPortSettings::Parity::kMark:  tty.c_cflag |= PARENB | CMSPAR | PARODD; break;
        case SerialPortSettings::Parity::kSpace: tty.c_cflag |= PARENB | CMSPAR; break;
    }
    if (s.stop != SerialPortSettings::StopBits::kOne) tty.c_cflag |= CSTOPB;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;
    if (::tcsetattr(impl_->fd, TCSANOW, &tty) != 0) {
        error = static_cast<uint32_t>(errno);
        return SerialConfigureResult::kRejected;
    }
    SetControlLines(s.dtr, s.rts);
    return SerialConfigureResult::kApplied;
}

void HostSerialPort::SetControlLines(bool dtr, bool rts) {
    const int set   = (dtr ? TIOCM_DTR : 0) | (rts ? TIOCM_RTS : 0);
    const int clear = (dtr ? 0 : TIOCM_DTR) | (rts ? 0 : TIOCM_RTS);
    if (set != 0) ::ioctl(impl_->fd, TIOCMBIS, &set);
    if (clear != 0) ::ioctl(impl_->fd, TIOCMBIC, &clear);
}

bool HostSerialPort::ModemLines(uint8_t& lines) {
    int status = 0;
    if (::ioctl(impl_->fd, TIOCMGET, &status) != 0) return false;
    lines = 0;
    if (status & TIOCM_CTS) lines |= kModemCts;
    if (status & TIOCM_DSR) lines |= kModemDsr;
    if (status & TIOCM_RNG) lines |= kModemRing;
    if (status & TIOCM_CAR) lines |= kModemDcd;
    return true;
}

SerialIoResult HostSerialPort::Read(uint8_t* buf, uint32_t size, uint32_t& got,
                                    uint32_t& error) {
    got = 0;
    switch (WaitFor(impl_->fd, POLLIN, impl_->stop, impl_->read_timeout_ms)) {
        case WaitResult::kStopped: return SerialIoResult::kStopped;
        case WaitResult::kTimeout: return SerialIoResult::kDone;
        case WaitResult::kError:
            error = static_cast<uint32_t>(errno);
            return SerialIoResult::kFailed;
        case WaitResult::kReady: break;
    }
    for (;;) {
        const ssize_t n = ::read(impl_->fd, buf, size);
        if (n > 0) {
            got = static_cast<uint32_t>(n);
            return SerialIoResult::kDone;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && errno == EAGAIN) return SerialIoResult::kDone;
        error = static_cast<uint32_t>(n < 0 ? errno : EIO);
        return SerialIoResult::kFailed;
    }
}

SerialIoResult HostSerialPort::WriteByte(uint8_t value, uint32_t& wrote, uint32_t& error) {
    wrote = 0;
    switch (WaitFor(impl_->fd, POLLOUT, impl_->stop, impl_->write_timeout_ms)) {
        case WaitResult::kStopped: return SerialIoResult::kStopped;
        case WaitResult::kTimeout: return SerialIoResult::kDone;
        case WaitResult::kError:
            error = static_cast<uint32_t>(errno);
            return SerialIoResult::kFailed;
        case WaitResult::kReady: break;
    }
    for (;;) {
        const ssize_t n = ::write(impl_->fd, &value, 1u);
        if (n >= 0) {
            wrote = static_cast<uint32_t>(n);
            return SerialIoResult::kDone;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN) return SerialIoResult::kDone;
        error = static_cast<uint32_t>(errno);
        return SerialIoResult::kFailed;
    }
}

bool HostSerialPort::PaceWait(int64_t monotonic_ticks) {
    if (impl_->stop < 0) return true;
    const int64_t freq = MonotonicFrequency();
    const int64_t ns   = monotonic_ticks / freq * 1000000000 +
                         monotonic_ticks % freq * 1000000000 / freq;
    timespec deadline{};
    ::clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += static_cast<time_t>(ns / 1000000000);
    deadline.tv_nsec += static_cast<long>(ns % 1000000000);
    if (deadline.tv_nsec >= 1000000000) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000;
    }
    pollfd stop{impl_->stop, POLLIN, 0};
    for (;;) {
        timespec now{};
        ::clock_gettime(CLOCK_MONOTONIC, &now);
        timespec left{deadline.tv_sec - now.tv_sec, deadline.tv_nsec - now.tv_nsec};
        if (left.tv_nsec < 0) {
            left.tv_sec -= 1;
            left.tv_nsec += 1000000000;
        }
        if (left.tv_sec < 0) return true;
        const int ready = ::ppoll(&stop, 1, &left, nullptr);
        if (ready == 0) return true;
        if (ready > 0) return false;
        if (errno != EINTR) return false;
    }
}

void HostSerialPort::RequestStop() {
    if (impl_->stop < 0) return;
    const uint64_t one = 1u;
    ssize_t        written;
    do {
        written = ::write(impl_->stop, &one, sizeof(one));
    } while (written < 0 && errno == EINTR);
}

bool HostSerialPort::WaitStopped(uint32_t timeout_ms) {
    pollfd stop{impl_->stop, POLLIN, 0};
    for (;;) {
        const int ready = ::poll(&stop, 1, static_cast<int>(timeout_ms));
        if (ready < 0 && errno == EINTR) continue;
        return ready > 0;
    }
}

std::vector<std::string> EnumerateSerialPorts() {
    std::vector<std::string> prefixes;
    std::ifstream            drivers("/proc/tty/drivers");
    std::string              line;
    while (std::getline(drivers, line)) {
        std::istringstream fields(line);
        std::string        driver, device, major, minors, type;
        if (!(fields >> driver >> device >> major >> minors >> type)) continue;
        if (type != "serial" || device.rfind("/dev/", 0) != 0) continue;
        prefixes.push_back(device.substr(5));
    }
    std::vector<std::string> ports;
    DIR* dev = ::opendir("/dev");
    if (dev == nullptr) return ports;
    while (const dirent* entry = ::readdir(dev)) {
        const std::string name = entry->d_name;
        const bool matches = std::any_of(prefixes.begin(), prefixes.end(), [&](const std::string& p) {
            return name.size() > p.size() && name.compare(0, p.size(), p) == 0;
        });
        if (matches && !PortHasNoUart(name)) ports.push_back("/dev/" + name);
    }
    ::closedir(dev);
    std::sort(ports.begin(), ports.end());
    return ports;
}

}

}
