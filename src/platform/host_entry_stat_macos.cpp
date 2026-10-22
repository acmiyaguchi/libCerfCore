#include "host_entry_stat.h"

namespace cerfcore {

namespace host_platform {

namespace {

void FillEntryStat(const struct stat& st, EntryStat& out) {
    out.is_dir   = S_ISDIR(st.st_mode);
    out.writable = (st.st_mode & 0222u) != 0u;
    out.size     = static_cast<uint64_t>(st.st_size);
    out.mtime    = st.st_mtimespec.tv_sec;
    out.btime    = st.st_birthtimespec.tv_sec;
}

}

bool ReadEntryStat(const char* path, EntryStat& out) {
    struct stat st {};
    if (stat(path, &st) != 0) return false;
    FillEntryStat(st, out);
    return true;
}

bool ReadEntryStatFd(int fd, EntryStat& out) {
    struct stat st {};
    if (fstat(fd, &st) != 0) return false;
    FillEntryStat(st, out);
    return true;
}

timespec StatModifiedTime(const struct stat& st) { return st.st_mtimespec; }

}

}
