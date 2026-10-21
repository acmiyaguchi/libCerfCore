#include "host_shared_entry.h"

#include "host_win32_name_rules.h"

#include <cerfcore/core/utf16.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <vector>

namespace cerfcore {

namespace host_platform {

namespace {

constexpr char    kDosAttribXattr[]  = "user.DOSATTRIB";
constexpr uint8_t kStoredAttributes  = kSharedHidden | kSharedSystem | kSharedArchive;
constexpr int     kDosEpochYear      = 1980;
constexpr int     kDosLastYear       = 2107;

struct EntryStat {
    bool     is_dir   = false;
    bool     writable = false;
    uint64_t size     = 0;
    int64_t  mtime    = 0;
    int64_t  btime    = 0;
};

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

bool ParseDosAttrib(char* data, ssize_t len, uint8_t& bits) {
    if (len <= 2 || data[0] != '0' || data[1] != 'x') return false;
    data[len]       = '\0';
    char*      end  = nullptr;
    const long value = std::strtol(data, &end, 16);
    if (*end != '\0') return false;
    bits = static_cast<uint8_t>(value);
    return true;
}

int StoredDosAttrib(ssize_t len, int err, char* data, bool& has, uint8_t& bits) {
    has = false;
    if (len < 0) return err == ENODATA || err == ENOTSUP || err == ERANGE ? 0 : err;
    has = ParseDosAttrib(data, len, bits);
    return 0;
}

void FillInfo(const EntryStat& st, bool stored, uint8_t bits, SharedFsInfo& info) {
    uint8_t a = st.is_dir ? static_cast<uint8_t>(kSharedDirectory) : uint8_t{0};
    if (!st.is_dir && !st.writable) a |= kSharedReadOnly;
    if (stored) {
        a |= bits & kStoredAttributes;
        if (st.is_dir) a |= bits & kSharedReadOnly;
    } else if (!st.is_dir) {
        a |= kSharedArchive;
    }
    info.attributes  = a;
    info.size        = st.is_dir ? 0u : st.size;
    info.write_time  = DosDateTimeOf(st.mtime);
    info.create_time = DosDateTimeOf(st.btime);
}

SharedFsStatus WriteDosAttrib(const std::string& path, uint8_t stored, uint8_t implied) {
    if (stored == implied) {
        if (removexattr(path.c_str(), kDosAttribXattr) != 0 && errno != ENODATA && errno != ENOTSUP) {
            return SharedErrno(errno, path);
        }
        return {};
    }
    char      value[8];
    const int len = std::snprintf(value, sizeof(value), "0x%x", stored);
    if (setxattr(path.c_str(), kDosAttribXattr, value, static_cast<size_t>(len), 0) != 0) {
        return SharedErrno(errno, path);
    }
    return {};
}

int64_t CurrentUtcOffset() {
    const std::time_t now = std::time(nullptr);
    std::tm           local{};
    localtime_r(&now, &local);
    return local.tm_gmtoff;
}

}

std::string SharedHostPath(const std::u16string& path) { return Utf16ToUtf8(path); }

std::string JoinSharedPath(const std::string& dir, const std::string& name) {
    return dir == "/" ? "/" + name : dir + "/" + name;
}

bool SharedDirectoryExists(const std::string& dir) {
    struct stat st {};
    return stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

SharedFsStatus SharedStatus(SharedFsError error, int err) {
    SharedFsStatus st;
    st.error  = error;
    st.native = static_cast<uint32_t>(err);
    return st;
}

SharedFsStatus SharedErrno(int err, const std::string& host_path) {
    switch (err) {
        case ENOENT: {
            std::string dir;
            std::string name;
            SplitSharedPath(host_path, dir, name);
            return SharedStatus(SharedDirectoryExists(dir) ? SharedFsError::kFileNotFound
                                                           : SharedFsError::kPathNotFound, err);
        }
        case ENOTDIR:      return SharedStatus(SharedFsError::kPathNotFound, err);
        case EACCES:
        case EPERM:
        case EISDIR:       return SharedStatus(SharedFsError::kAccessDenied, err);
        case EMFILE:
        case ENFILE:       return SharedStatus(SharedFsError::kTooManyOpenFiles, err);
        case EBADF:        return SharedStatus(SharedFsError::kInvalidHandle, err);
        case EROFS:        return SharedStatus(SharedFsError::kWriteProtect, err);
        case ENOSPC:
        case EDQUOT:       return SharedStatus(SharedFsError::kDiskFull, err);
        case EEXIST:       return SharedStatus(SharedFsError::kFileExists, err);
        case ENAMETOOLONG: return SharedStatus(SharedFsError::kInvalidName, err);
        case ENOTEMPTY:    return SharedStatus(SharedFsError::kDirNotEmpty, err);
        default:           return SharedStatus(SharedFsError::kUnmapped, err);
    }
}

void SplitSharedPath(const std::string& host_path, std::string& dir, std::string& name) {
    std::string path = host_path;
    while (path.size() > 1u && path.back() == '/') path.pop_back();
    const size_t slash = path.rfind('/');
    if (slash == std::string::npos) {
        dir  = ".";
        name = path;
        return;
    }
    dir  = slash == 0u ? "/" : path.substr(0, slash);
    name = path.substr(slash + 1u);
}

bool FindSharedEntry(const std::string& dir, std::u16string_view name, std::string& found) {
    const std::string exact = Utf16ToUtf8(name);
    struct stat       st {};
    if (Utf8ToUtf16(exact) == name && lstat(JoinSharedPath(dir, exact).c_str(), &st) == 0) {
        found = exact;
        return true;
    }
    DIR* d = opendir(dir.c_str());
    if (d == nullptr) return false;
    bool hit = false;
    while (const dirent* e = readdir(d)) {
        const std::u16string candidate = Utf8ToUtf16(e->d_name);
        if (SameWin32Name(candidate, name) && Utf16ToUtf8(candidate) == e->d_name) {
            found = e->d_name;
            hit   = true;
            break;
        }
    }
    closedir(d);
    return hit;
}

SharedFsStatus LocateSharedEntry(const std::string& host_path, std::string& actual) {
    if (host_path.size() > 1u && host_path.back() == '/') {
        actual = host_path.substr(0, host_path.size() - 1u);
        struct stat st {};
        if (stat(actual.c_str(), &st) != 0) return SharedErrno(errno, actual);
        return {};
    }
    std::string dir;
    std::string name;
    SplitSharedPath(host_path, dir, name);
    std::string found;
    if (FindSharedEntry(dir, Utf8ToUtf16(name), found)) {
        actual = JoinSharedPath(dir, found);
        return {};
    }
    return SharedStatus(SharedDirectoryExists(dir) ? SharedFsError::kFileNotFound
                                                   : SharedFsError::kPathNotFound, ENOENT);
}

SharedFsStatus DescribeSharedEntry(const std::string& path, SharedFsInfo& info) {
    EntryStat st;
    if (!ReadStatx(AT_FDCWD, path.c_str(), 0, st)) return SharedErrno(errno, path);
    char          data[65];
    uint8_t       bits = 0;
    bool          has  = false;
    const ssize_t len  = getxattr(path.c_str(), kDosAttribXattr, data, sizeof(data) - 1u);
    if (const int err = StoredDosAttrib(len, errno, data, has, bits); err != 0) {
        return SharedErrno(err, path);
    }
    FillInfo(st, has, bits, info);
    return {};
}

SharedFsStatus DescribeSharedFd(int fd, SharedFsInfo& info) {
    EntryStat st;
    if (!ReadStatx(fd, "", AT_EMPTY_PATH, st)) return SharedStatus(SharedFsError::kInvalidHandle, errno);
    char          data[65];
    uint8_t       bits = 0;
    bool          has  = false;
    const ssize_t len  = fgetxattr(fd, kDosAttribXattr, data, sizeof(data) - 1u);
    if (const int err = StoredDosAttrib(len, errno, data, has, bits); err != 0) {
        return SharedStatus(SharedFsError::kUnmapped, err);
    }
    FillInfo(st, has, bits, info);
    return {};
}

SharedFsStatus StoreSharedAttributes(const std::string& path, uint8_t attributes) {
    struct stat st {};
    if (stat(path.c_str(), &st) != 0) return SharedErrno(errno, path);
    const bool    is_dir  = S_ISDIR(st.st_mode);
    const uint8_t stored  = static_cast<uint8_t>((attributes & kStoredAttributes) |
                                                 (is_dir ? attributes & kSharedReadOnly : 0u));
    const uint8_t implied = is_dir ? uint8_t{0} : static_cast<uint8_t>(kSharedArchive);
    if (is_dir) return WriteDosAttrib(path, stored, implied);
    const mode_t mode     = st.st_mode & 07777u;
    const mode_t writable = (mode & 0222u) != 0u ? mode : mode | S_IWUSR;
    if (writable != mode && chmod(path.c_str(), writable) != 0) return SharedErrno(errno, path);
    const SharedFsStatus xattr  = WriteDosAttrib(path, stored, implied);
    const mode_t         target = (attributes & kSharedReadOnly) != 0u ? writable & ~0222u : writable;
    const mode_t         final  = xattr.Ok() ? target : mode;
    if (final != writable && chmod(path.c_str(), final) != 0) return SharedErrno(errno, path);
    return xattr;
}

bool CopySharedAttributes(int from_fd, int to_fd) {
    std::vector<char> data;
    for (;;) {
        const ssize_t size = fgetxattr(from_fd, kDosAttribXattr, nullptr, 0);
        if (size < 0) return errno == ENODATA || errno == ENOTSUP;
        data.resize(static_cast<size_t>(size));
        const ssize_t len = fgetxattr(from_fd, kDosAttribXattr, data.data(), data.size());
        if (len >= 0) {
            data.resize(static_cast<size_t>(len));
            break;
        }
        if (errno != ERANGE) return errno == ENODATA;
    }
    return fsetxattr(to_fd, kDosAttribXattr, data.data(), data.size(), 0) == 0;
}

uint32_t DosDateTimeOf(int64_t utc_seconds) {
    using namespace std::chrono;
    const sys_seconds           local{seconds{utc_seconds + CurrentUtcOffset()}};
    const sys_days              day = floor<days>(local);
    const year_month_day        ymd{day};
    const hh_mm_ss<seconds>     hms{local - day};
    const int                   y = static_cast<int>(ymd.year());
    if (y < kDosEpochYear || y > kDosLastYear) return 0u;
    const uint32_t date = (static_cast<uint32_t>(y - kDosEpochYear) << 9) |
                          (static_cast<unsigned>(ymd.month()) << 5) | static_cast<unsigned>(ymd.day());
    const uint32_t time = (static_cast<uint32_t>(hms.hours().count()) << 11) |
                          (static_cast<uint32_t>(hms.minutes().count()) << 5) |
                          (static_cast<uint32_t>(hms.seconds().count()) / 2u);
    return (date << 16) | time;
}

bool UtcOfDosDateTime(uint32_t dos, int64_t& utc_seconds) {
    using namespace std::chrono;
    const uint32_t       date = dos >> 16;
    const uint32_t       time = dos & 0xFFFFu;
    const year_month_day ymd{year{static_cast<int>(date >> 9) + kDosEpochYear},
                             month{(date >> 5) & 0x0Fu}, day{date & 0x1Fu}};
    const uint32_t hour   = time >> 11;
    const uint32_t minute = (time >> 5) & 0x3Fu;
    const uint32_t second = (time & 0x1Fu) * 2u;
    if (!ymd.ok() || hour > 23u || minute > 59u || second > 59u) return false;
    const int64_t local = static_cast<int64_t>(sys_days{ymd}.time_since_epoch().count()) * 86400 +
                          static_cast<int64_t>(hour * 3600u + minute * 60u + second);
    utc_seconds = local - CurrentUtcOffset();
    return true;
}

}

}
