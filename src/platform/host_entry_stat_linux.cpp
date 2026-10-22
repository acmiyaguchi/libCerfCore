#include "host_entry_stat.h"

#include <fcntl.h>

namespace cerfcore {

namespace host_platform {

namespace {

bool ReadStatx(int dirfd, const char* path, int flags, EntryStat& out) {
    struct statx sx {};
    if (statx(dirfd, path, flags | AT_STATX_SYNC_AS_STAT, STATX_BASIC_STATS | STATX_BTIME, &sx) != 0) {
        return false;
    }
    out.is_dir   = S_ISDIR(sx.stx_mode);
    out.writable = (sx.stx_mode & 0222u) != 0u;
    out.size     = sx.stx_size;
    out.mtime    = sx.stx_mtime.tv_sec;
    out.btime    = (sx.stx_mask & STATX_BTIME) != 0u ? sx.stx_btime.tv_sec : out.mtime;
    return true;
}

}

bool ReadEntryStat(const char* path, EntryStat& out) { return ReadStatx(AT_FDCWD, path, 0, out); }

bool ReadEntryStatFd(int fd, EntryStat& out) { return ReadStatx(fd, "", AT_EMPTY_PATH, out); }

timespec StatModifiedTime(const struct stat& st) { return st.st_mtim; }

}

}
