#include <cerfcore/platform/host_descriptor.h>

#include <fcntl.h>

namespace cerfcore {

namespace host_platform {

bool SetCloseOnExec(int fd) {
    const int flags = ::fcntl(fd, F_GETFD);
    return flags >= 0 && ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

bool SetCloseOnExecNonBlocking(int fd) {
    const int status = ::fcntl(fd, F_GETFL);
    return SetCloseOnExec(fd) && status >= 0 && ::fcntl(fd, F_SETFL, status | O_NONBLOCK) == 0;
}

}

}
