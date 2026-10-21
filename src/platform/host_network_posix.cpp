#include "host_network.h"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace cerfcore {

namespace host_platform {

/* send(2) EPIPE: the process receives SIGPIPE unless MSG_NOSIGNAL is set;
   libslirp sends with flags 0, so the process ignores SIGPIPE (QEMU
   os-posix.c os_setup_early_signal_handling). */
bool StartNetworking(int& error) {
    struct sigaction act {};
    sigemptyset(&act.sa_mask);
    act.sa_handler = SIG_IGN;
    error          = sigaction(SIGPIPE, &act, nullptr) == 0 ? 0 : errno;
    return error == 0;
}

TcpConnectProbe ProbeTcpConnect6(const uint8_t (&address)[16], uint16_t port, int timeout_ms) {
    TcpConnectProbe out;
    const int s = ::socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) {
        out.error = errno;
        return out;
    }
    out.socket_created = true;
    ::fcntl(s, F_SETFL, ::fcntl(s, F_GETFL) | O_NONBLOCK);
    sockaddr_in6 dst{};
    dst.sin6_family = AF_INET6;
    dst.sin6_port   = htons(port);
    std::memcpy(&dst.sin6_addr, address, sizeof(address));
    const int rc  = ::connect(s, reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
    int       err = rc == 0 ? 0 : errno;
    if (rc == 0) {
        out.reachable = true;
    } else if (err == EINPROGRESS) {
        pollfd pfd{};
        pfd.fd     = s;
        pfd.events = POLLOUT;
        const int pr = ::poll(&pfd, 1, timeout_ms);
        if (pr > 0 && (pfd.revents & POLLOUT) != 0 && (pfd.revents & (POLLERR | POLLHUP)) == 0) {
            int       so_err = 0;
            socklen_t len    = sizeof(so_err);
            if (::getsockopt(s, SOL_SOCKET, SO_ERROR, &so_err, &len) == 0 && so_err == 0) {
                out.reachable = true;
            } else {
                err = so_err;
            }
        }
    }
    ::close(s);
    out.error = out.reachable ? 0 : err;
    return out;
}

struct HostPollSet::Entries {
    std::vector<pollfd> fds;
};

HostPollSet::HostPollSet() : entries_(std::make_unique<Entries>()) {}
HostPollSet::~HostPollSet() = default;

void HostPollSet::Clear() { entries_->fds.clear(); }
bool HostPollSet::Empty() const { return entries_->fds.empty(); }

int HostPollSet::Add(uintptr_t socket, int events) {
    pollfd pfd{};
    pfd.fd = static_cast<int>(socket);
    if (events & kPollReadable) pfd.events |= POLLIN;
    if (events & kPollWritable) pfd.events |= POLLOUT;
    if (events & kPollPriority) pfd.events |= POLLPRI;
    entries_->fds.push_back(pfd);
    return static_cast<int>(entries_->fds.size()) - 1;
}

int HostPollSet::Revents(int index) const {
    if (index < 0 || index >= static_cast<int>(entries_->fds.size())) return 0;
    const short revents = entries_->fds[static_cast<size_t>(index)].revents;
    int         out     = 0;
    if (revents & POLLIN)  out |= kPollReadable;
    if (revents & POLLOUT) out |= kPollWritable;
    if (revents & POLLPRI) out |= kPollPriority;
    if (revents & POLLERR) out |= kPollError;
    if (revents & POLLHUP) out |= kPollHangUp;
    return out;
}

int HostPollSet::Wait(int timeout_ms) {
    return ::poll(entries_->fds.data(), static_cast<nfds_t>(entries_->fds.size()), timeout_ms);
}

}

}
