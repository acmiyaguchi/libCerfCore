#pragma once

#include <cstdint>

namespace cerfcore {

namespace host_platform {

class HostStopWait {
public:
    HostStopWait() = default;
    ~HostStopWait();
    HostStopWait(const HostStopWait&)            = delete;
    HostStopWait& operator=(const HostStopWait&) = delete;

    bool Open(int stop_fd);
    void Close();
    bool Sleep(int64_t ns);

private:
    int fd_ = -1;
};

}

}
