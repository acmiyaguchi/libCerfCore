#pragma once

#include <cstdint>
#include <sys/stat.h>
#include <time.h>

namespace cerfcore {

namespace host_platform {

struct EntryStat {
    bool     is_dir   = false;
    bool     writable = false;
    uint64_t size     = 0;
    int64_t  mtime    = 0;
    int64_t  btime    = 0;
};

bool     ReadEntryStat(const char* path, EntryStat& out);
bool     ReadEntryStatFd(int fd, EntryStat& out);
timespec StatModifiedTime(const struct stat& st);

}

}
