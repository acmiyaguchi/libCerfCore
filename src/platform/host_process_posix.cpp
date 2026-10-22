#include <cerfcore/platform/host_process.h>

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include "host_app_bundle.h"
#include "host_unimplemented.h"

namespace cerfcore {

namespace host_platform {

namespace {

bool WriteAll(int fd, const char* buf, size_t len) {
    size_t done = 0;
    while (done < len) {
        const ssize_t n = ::write(fd, buf + done, len - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}

}

std::string ExecutablePath() {
#if defined(__linux__)
    std::string buf(256, '\0');
    for (;;) {
        const ssize_t n = ::readlink("/proc/self/exe", &buf[0], buf.size());
        if (n < 0) return {};
        if (static_cast<size_t>(n) < buf.size()) {
            buf.resize(static_cast<size_t>(n));
            return buf;
        }
        if (buf.size() >= 32768) return {};
        buf.resize(buf.size() * 2);
    }
#elif defined(__APPLE__)
    std::string path(1024, '\0');
    uint32_t    size = static_cast<uint32_t>(path.size());
    if (::_NSGetExecutablePath(&path[0], &size) != 0) {
        path.assign(size, '\0');
        if (::_NSGetExecutablePath(&path[0], &size) != 0) return {};
    }
    char* real = ::realpath(path.c_str(), nullptr);
    if (real == nullptr) return {};
    std::string resolved(real);
    std::free(real);
    return resolved;
#else
    NoHostImplementation("ExecutablePath");
#endif
}

std::string ApplicationPath() {
    const std::string exe = ExecutablePath();
#if defined(__APPLE__)
    const size_t bundle = AppBundleLength(exe.data(), exe.size());
    if (bundle != 0) return exe.substr(0, bundle);
#endif
    return exe;
}

uint32_t CurrentProcessId() { return static_cast<uint32_t>(::getpid()); }

bool StdoutAvailable() { return ::fcntl(STDOUT_FILENO, F_GETFD) != -1; }

bool WriteStdout(const char* buf, size_t len) { return WriteAll(STDOUT_FILENO, buf, len); }

bool WriteStderr(const char* buf, size_t len) { return WriteAll(STDERR_FILENO, buf, len); }

void ExitProcessNow(int code) { std::_Exit(code); }

}

}
