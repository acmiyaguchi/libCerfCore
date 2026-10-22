#pragma once

#include <cstdint>
#include <memory>

namespace cerfcore {

namespace host_platform {

enum class SerialWaitResult { kReady, kTimeout, kStopped, kError };

class SerialPortWait {
public:
    SerialPortWait();
    ~SerialPortWait();
    SerialPortWait(const SerialPortWait&)            = delete;
    SerialPortWait& operator=(const SerialPortWait&) = delete;

    bool Open(int port_fd, int stop_fd);
    void Close();
    SerialWaitResult WaitReadable(uint32_t timeout_ms);
    SerialWaitResult WaitWritable(uint32_t timeout_ms);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}

}
