#pragma once

#include <cstdint>
#include <memory>

namespace host_platform {

bool StartNetworking(int& error);

struct TcpConnectProbe {
    bool socket_created = false;
    bool reachable      = false;
    int  error          = 0;
};

TcpConnectProbe ProbeTcpConnect6(const uint8_t (&address)[16], uint16_t port, int timeout_ms);

enum PollEvent : int {
    kPollReadable = 1 << 0,
    kPollWritable = 1 << 1,
    kPollPriority = 1 << 2,
    kPollError    = 1 << 3,
    kPollHangUp   = 1 << 4,
};

class HostPollSet {
public:
    HostPollSet();
    ~HostPollSet();
    HostPollSet(const HostPollSet&)            = delete;
    HostPollSet& operator=(const HostPollSet&) = delete;

    void Clear();
    bool Empty() const;
    int  Add(uintptr_t socket, int events);
    int  Revents(int index) const;
    int  Wait(int timeout_ms);

private:
    struct Entries;
    std::unique_ptr<Entries> entries_;
};

}
