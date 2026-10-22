#include "host_process_memory.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace cerfcore {

namespace host_platform {

namespace {

constexpr size_t kStatusBytes = 4096;

size_t ReadProcSelfStatus(char (&text)[kStatusBytes]) {
    const int fd = ::open("/proc/self/status", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    size_t used = 0;
    while (used + 1u < sizeof(text)) {
        const ssize_t got = ::read(fd, text + used, sizeof(text) - 1u - used);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        used += static_cast<size_t>(got);
    }
    ::close(fd);
    text[used] = '\0';
    return used;
}

uint64_t StatusField(const char* text, const char* name) {
    const size_t name_length = std::strlen(name);
    for (const char* line = text; *line != '\0';) {
        if (std::strncmp(line, name, name_length) == 0 && line[name_length] == ':') {
            return std::strtoull(line + name_length + 1, nullptr, 10);
        }
        const char* next = std::strchr(line, '\n');
        if (next == nullptr) break;
        line = next + 1;
    }
    return 0;
}

}

ProcessMemoryUsage QueryProcessMemory() {
    ProcessMemoryUsage out;
    char               text[kStatusBytes];
    if (ReadProcSelfStatus(text) == 0) return out;
    out.private_bytes = (StatusField(text, "VmData") + StatusField(text, "VmStk")) * 1024u;
    out.working_set   = StatusField(text, "VmRSS") * 1024u;
    return out;
}

ThreadStackUsage MeasureThreadStacks() {
    ThreadStackUsage out;
    char             text[kStatusBytes];
    if (ReadProcSelfStatus(text) == 0) return out;
    out.count = StatusField(text, "Threads");
    return out;
}

}

}
