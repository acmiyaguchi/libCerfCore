#include "host_shared_folder.h"

#include "host_entry_stat.h"
#include "host_rename_entry.h"
#include "host_share_modes.h"
#include "host_shared_entry.h"
#include "host_win32_name_rules.h"

#include <cerfcore/core/utf16.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace cerfcore {

namespace host_platform {

namespace {

struct FindState {
    ~FindState() {
        if (hold_fd >= 0) close(hold_fd);
    }

    std::string                                         dir;
    std::vector<std::pair<std::string, std::u16string>> names;
    size_t                                              next    = 0;
    int                                                 hold_fd = -1;
};

struct FileState {
    int fd      = -1;
    int lock_fd = -1;
};

bool NormalizeRoot(const std::string& root, std::vector<std::string>& segments) {
    if (root.empty()) return false;
    std::string path = root;
    if (path[0] != '/') {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == nullptr) return false;
        path = std::string(cwd) + "/" + path;
    }
    size_t i = 0;
    while (i < path.size()) {
        while (i < path.size() && path[i] == '/') ++i;
        const size_t start = i;
        while (i < path.size() && path[i] != '/') ++i;
        const std::string segment = path.substr(start, i - start);
        if (segment.empty() || segment == ".") continue;
        if (segment == "..") {
            if (!segments.empty()) segments.pop_back();
            continue;
        }
        segments.push_back(segment);
    }
    return !segments.empty();
}

SharedFsStatus NameStatus(const std::string& dir, const std::string& name, std::u16string& name16) {
    if (!SharedDirectoryExists(dir)) return SharedStatus(SharedFsError::kPathNotFound, ENOENT);
    name16 = Utf8ToUtf16(name);
    if (!IsWin32FileName(name16)) return SharedStatus(SharedFsError::kInvalidName, EINVAL);
    return {};
}

SharedFsStatus IoStatus(int err, SharedFsError fault) {
    switch (err) {
        case EBADF:
        case EINVAL: return SharedStatus(SharedFsError::kAccessDenied, err);
        case EIO:    return SharedStatus(fault, err);
        case ENOSPC:
        case EDQUOT: return SharedStatus(SharedFsError::kDiskFull, err);
        default:     return SharedStatus(SharedFsError::kUnmapped, err);
    }
}

bool CopyContents(int in, int out) {
    char buf[65536];
    for (;;) {
        const ssize_t got = read(in, buf, sizeof(buf));
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return got == 0;
        for (ssize_t done = 0; done < got;) {
            const ssize_t n = write(out, buf + done, static_cast<size_t>(got - done));
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) return false;
            done += n;
        }
    }
}

SharedFsStatus MoveAcrossDevices(const std::string& src, const std::string& dst) {
    struct stat st {};
    if (stat(src.c_str(), &st) != 0) return SharedErrno(errno, src);
    if (!S_ISREG(st.st_mode)) return SharedStatus(SharedFsError::kUnmapped, EXDEV);
    const int in = open(src.c_str(), O_RDONLY | O_CLOEXEC);
    if (in < 0) return SharedErrno(errno, src);
    const mode_t mode = st.st_mode & 07777u;
    const int    out  = open(dst.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode | S_IWUSR);
    if (out < 0) {
        const int err = errno;
        close(in);
        return err == EEXIST ? SharedStatus(SharedFsError::kAlreadyExists, err) : SharedErrno(err, dst);
    }
    bool ok = CopyContents(in, out) && CopySharedAttributes(in, out);
    int  err = ok ? 0 : errno;
    if (ok) {
        const struct timespec times[2] = {{0, UTIME_OMIT}, StatModifiedTime(st)};
        ok = futimens(out, times) == 0 && fchmod(out, mode) == 0;
        if (!ok) err = errno;
    }
    close(in);
    if (close(out) != 0 && ok) {
        ok  = false;
        err = errno;
    }
    if (!ok) {
        unlink(dst.c_str());
        return SharedErrno(err, dst);
    }
    if (unlink(src.c_str()) != 0) return SharedErrno(errno, src);
    return {};
}

bool SameInode(const std::string& a, const std::string& b) {
    struct stat sa {};
    struct stat sb {};
    return lstat(a.c_str(), &sa) == 0 && lstat(b.c_str(), &sb) == 0 && sa.st_dev == sb.st_dev &&
           sa.st_ino == sb.st_ino;
}

}

SharedPathResult ResolveSharedPath(const std::string& root, std::u16string_view guest_name,
                                   std::u16string& out) {
    std::vector<std::string> root_segments;
    if (!NormalizeRoot(root, root_segments)) return SharedPathResult::kRootUnavailable;
    std::vector<Win32PathSegment> base;
    for (const std::string& s : root_segments) base.push_back(Win32PathSegment{Utf8ToUtf16(s), false});
    const Win32FullPath full = NormalizeWin32Path(base, guest_name);
    if (full.segments.size() < base.size()) return SharedPathResult::kInvalidName;
    for (size_t i = 0; i < base.size(); ++i) {
        if (!SameWin32Name(full.segments[i].text, base[i].text)) return SharedPathResult::kInvalidName;
    }
    std::string path;
    for (const std::string& s : root_segments) path += "/" + s;
    for (size_t i = base.size(); i < full.segments.size(); ++i) {
        const bool        literal = i + 1u == full.segments.size() && !full.trailing_separator;
        const std::string name    = Utf16ToUtf8(full.segments[i].text);
        std::string       found;
        path = JoinSharedPath(path, !literal && FindSharedEntry(path, full.segments[i].text, found)
                                        ? found
                                        : name);
    }
    if (full.trailing_separator) path += "/";
    out = Utf8ToUtf16(path);
    return SharedPathResult::kResolved;
}

SharedFsStatus CreateSharedDirectory(const std::u16string& path) {
    std::string dir;
    std::string name;
    SplitSharedPath(SharedHostPath(path), dir, name);
    std::u16string name16;
    const SharedFsStatus st = NameStatus(dir, name, name16);
    if (!st.Ok()) return st;
    std::string found;
    if (FindSharedEntry(dir, name16, found)) return SharedStatus(SharedFsError::kAlreadyExists, EEXIST);
    const std::string target = JoinSharedPath(dir, name);
    if (mkdir(target.c_str(), 0777) != 0) {
        return errno == EEXIST ? SharedStatus(SharedFsError::kAlreadyExists, errno)
                               : SharedErrno(errno, target);
    }
    return {};
}

SharedFsStatus RemoveSharedDirectory(const std::u16string& path) {
    std::string    actual;
    SharedFsStatus st = LocateSharedEntry(SharedHostPath(path), actual);
    if (!st.Ok()) return st;
    SharedFsInfo info;
    st = DescribeSharedEntry(actual, info);
    if (!st.Ok()) return st;
    if ((info.attributes & kSharedDirectory) == 0u) return SharedStatus(SharedFsError::kUnmapped, ENOTDIR);
    if ((info.attributes & kSharedReadOnly) != 0u) return SharedStatus(SharedFsError::kAccessDenied, EACCES);
    if (rmdir(actual.c_str()) != 0) {
        return (errno == ENOTEMPTY || errno == EEXIST) ? SharedStatus(SharedFsError::kDirNotEmpty, errno)
                                                       : SharedErrno(errno, actual);
    }
    return {};
}

SharedFsStatus DeleteSharedFile(const std::u16string& path) {
    std::string    actual;
    SharedFsStatus st = LocateSharedEntry(SharedHostPath(path), actual);
    if (!st.Ok()) return st;
    SharedFsInfo info;
    st = DescribeSharedEntry(actual, info);
    if (!st.Ok()) return st;
    if ((info.attributes & (kSharedDirectory | kSharedReadOnly)) != 0u) {
        return SharedStatus(SharedFsError::kAccessDenied, EACCES);
    }
    st = CheckSharedEntryFree(actual);
    if (!st.Ok()) return st;
    if (unlink(actual.c_str()) != 0) return SharedErrno(errno, actual);
    return {};
}

SharedFsStatus MoveSharedFile(const std::u16string& from, const std::u16string& to) {
    std::string    src;
    SharedFsStatus st = LocateSharedEntry(SharedHostPath(from), src);
    if (!st.Ok()) return st;
    st = CheckSharedEntryFree(src);
    if (!st.Ok()) return st;
    std::string dir;
    std::string name;
    SplitSharedPath(SharedHostPath(to), dir, name);
    std::u16string name16;
    st = NameStatus(dir, name, name16);
    if (!st.Ok()) return st;
    const std::string dst = JoinSharedPath(dir, name);
    std::string       found;
    const bool        renames_self = FindSharedEntry(dir, name16, found) &&
                                     SameInode(JoinSharedPath(dir, found), src);
    if (!found.empty() && !renames_self) return SharedStatus(SharedFsError::kAlreadyExists, EEXIST);
    if (RenameEntry(src.c_str(), dst.c_str(), !renames_self) == 0) return {};
    if (RenameNoReplaceUnsupported(errno) && rename(src.c_str(), dst.c_str()) == 0) return {};
    if (errno == EEXIST) return SharedStatus(SharedFsError::kAlreadyExists, errno);
    if (errno == EXDEV) return MoveAcrossDevices(src, dst);
    return SharedErrno(errno, dst);
}

SharedFsStatus SetSharedWriteTime(const std::u16string& path, uint32_t dos_time) {
    int64_t utc = 0;
    if (!UtcOfDosDateTime(dos_time, utc)) return SharedStatus(SharedFsError::kUnrepresentableTime, EINVAL);
    std::string    actual;
    SharedFsStatus st = LocateSharedEntry(SharedHostPath(path), actual);
    if (!st.Ok()) return st;
    const struct timespec times[2] = {{0, UTIME_OMIT}, {static_cast<time_t>(utc), 0}};
    if (utimensat(AT_FDCWD, actual.c_str(), times, 0) != 0) return SharedErrno(errno, actual);
    return {};
}

SharedFsStatus SetSharedAttributes(const std::u16string& path, uint8_t attributes) {
    std::string    actual;
    SharedFsStatus st = LocateSharedEntry(SharedHostPath(path), actual);
    if (!st.Ok()) return st;
    return StoreSharedAttributes(actual, attributes);
}

SharedFsStatus QuerySharedInfo(const std::u16string& path, SharedFsInfo& info) {
    std::string    actual;
    SharedFsStatus st = LocateSharedEntry(SharedHostPath(path), actual);
    if (!st.Ok()) return st;
    return DescribeSharedEntry(actual, info);
}

SharedFsStatus QuerySharedSpace(const std::string& root, uint64_t& total, uint64_t& available) {
    struct statvfs sv {};
    if (statvfs(root.c_str(), &sv) != 0) return SharedErrno(errno, root);
    total     = static_cast<uint64_t>(sv.f_blocks) * sv.f_frsize;
    available = static_cast<uint64_t>(sv.f_bavail) * sv.f_frsize;
    return {};
}

SharedFsStatus CreateNewSharedFile(const std::u16string& path) {
    std::string dir;
    std::string name;
    SplitSharedPath(SharedHostPath(path), dir, name);
    std::u16string name16;
    const SharedFsStatus st = NameStatus(dir, name, name16);
    if (!st.Ok()) return st;
    std::string found;
    if (FindSharedEntry(dir, name16, found)) return SharedStatus(SharedFsError::kFileExists, EEXIST);
    const std::string target = JoinSharedPath(dir, name);
    const int         fd = open(target.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOCTTY, 0666);
    if (fd < 0) return SharedErrno(errno, target);
    close(fd);
    return {};
}

SharedFsFind::~SharedFsFind() { delete static_cast<FindState*>(handle_); }

SharedFsStatus SharedFsFind::First(const std::u16string& pattern, SharedFsInfo& info,
                                   std::u16string& name) {
    const std::string host  = SharedHostPath(pattern);
    const size_t      slash = host.rfind('/');
    auto              state = std::make_unique<FindState>();
    state->dir              = slash == 0u ? "/" : host.substr(0, slash);
    const std::u16string mask = Utf8ToUtf16(host.substr(slash + 1u));
    if (mask.empty()) return SharedStatus(SharedFsError::kFileNotFound, ENOENT);
    if (!SharedDirectoryExists(state->dir)) return SharedStatus(SharedFsError::kPathNotFound, ENOENT);
    SharedFsStatus held = HoldSharedDirectory(state->dir, state->hold_fd);
    if (!held.Ok()) return held;
    if (!HasWin32Wildcard(mask)) {
        std::string found;
        if (!FindSharedEntry(state->dir, mask, found)) return SharedStatus(SharedFsError::kFileNotFound, ENOENT);
        state->names.emplace_back(found, Utf8ToUtf16(found));
    } else {
        DIR* d = opendir(state->dir.c_str());
        if (d == nullptr) return SharedErrno(errno, state->dir);
        const std::u16string fixed = FixWin32Mask(mask);
        while (const dirent* e = readdir(d)) {
            std::u16string candidate = Utf8ToUtf16(e->d_name);
            if (Utf16ToUtf8(candidate) != e->d_name || !IsWin32FileName(candidate)) continue;
            if (MatchWin32Mask(candidate, fixed)) state->names.emplace_back(e->d_name, std::move(candidate));
        }
        closedir(d);
        std::sort(state->names.begin(), state->names.end(),
                  [](const auto& a, const auto& b) { return Win32NameLess(a.second, b.second); });
    }
    handle_ = state.release();
    const SharedFsStatus st = Next(info, name);
    if (st.error != SharedFsError::kNoMoreFiles) return st;
    delete static_cast<FindState*>(handle_);
    handle_ = nullptr;
    return SharedStatus(SharedFsError::kFileNotFound, ENOENT);
}

SharedFsStatus SharedFsFind::Next(SharedFsInfo& info, std::u16string& name) {
    auto* state = static_cast<FindState*>(handle_);
    while (state != nullptr && state->next < state->names.size()) {
        const auto&          entry = state->names[state->next++];
        const SharedFsStatus st    = DescribeSharedEntry(JoinSharedPath(state->dir, entry.first), info);
        if (st.Ok()) {
            name = entry.second;
            return {};
        }
        if (st.native != ENOENT) return st;
    }
    return SharedStatus(SharedFsError::kNoMoreFiles, ENOENT);
}

SharedFsFile::~SharedFsFile() {
    auto* state = static_cast<FileState*>(handle_);
    if (state == nullptr) return;
    if (state->lock_fd >= 0 && state->lock_fd != state->fd) close(state->lock_fd);
    close(state->fd);
    delete state;
}

SharedFsStatus SharedFsFile::Open(const std::u16string& path, SharedFileAccess access,
                                  SharedFileShare share) {
    std::string    actual;
    SharedFsStatus st = LocateSharedEntry(SharedHostPath(path), actual);
    if (!st.Ok()) return st;
    int flags = O_CLOEXEC | O_NOCTTY | O_NONBLOCK;
    switch (access) {
        case SharedFileAccess::kRead:      flags |= O_RDONLY; break;
        case SharedFileAccess::kWrite:     flags |= O_WRONLY; break;
        case SharedFileAccess::kReadWrite: flags |= O_RDWR; break;
    }
    const int fd = open(actual.c_str(), flags);
    if (fd < 0) return SharedErrno(errno, actual);
    struct stat sb {};
    if (fstat(fd, &sb) != 0 || !S_ISREG(sb.st_mode) || fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) != 0) {
        close(fd);
        return SharedStatus(SharedFsError::kAccessDenied, EACCES);
    }
    const int lock_fd = access == SharedFileAccess::kWrite
                            ? open(actual.c_str(), O_RDONLY | O_CLOEXEC | O_NOCTTY | O_NONBLOCK)
                            : fd;
    if (lock_fd < 0) {
        const int err = errno;
        close(fd);
        return SharedErrno(err, actual);
    }
    ShareClaim claim;
    claim.read        = access != SharedFileAccess::kWrite;
    claim.write       = access != SharedFileAccess::kRead;
    claim.share_read  = share == SharedFileShare::kDenyWrite || share == SharedFileShare::kDenyNone;
    claim.share_write = share == SharedFileShare::kDenyRead || share == SharedFileShare::kDenyNone;
    st = ClaimShareMode(lock_fd, actual, claim);
    if (!st.Ok()) {
        if (lock_fd != fd) close(lock_fd);
        close(fd);
        return st;
    }
    handle_ = new FileState{fd, lock_fd};
    return {};
}

SharedFsStatus SharedFsFile::ReadAt(uint64_t position, void* buf, uint32_t size, uint32_t& done) {
    const auto* state = static_cast<FileState*>(handle_);
    if (state == nullptr) return SharedStatus(SharedFsError::kInvalidHandle, EBADF);
    auto*    out   = static_cast<uint8_t*>(buf);
    uint32_t total = 0;
    while (total < size) {
        const ssize_t n = pread(state->fd, out + total, size - total, static_cast<off_t>(position + total));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return IoStatus(errno, SharedFsError::kReadFault);
        if (n == 0) break;
        total += static_cast<uint32_t>(n);
    }
    done = total;
    return {};
}

SharedFsStatus SharedFsFile::WriteAt(uint64_t position, const void* buf, uint32_t size,
                                     uint32_t& done) {
    const auto* state = static_cast<FileState*>(handle_);
    if (state == nullptr) return SharedStatus(SharedFsError::kInvalidHandle, EBADF);
    const auto* in    = static_cast<const uint8_t*>(buf);
    uint32_t    total = 0;
    while (total < size) {
        const ssize_t n = pwrite(state->fd, in + total, size - total, static_cast<off_t>(position + total));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return IoStatus(n < 0 ? errno : EIO, SharedFsError::kWriteFault);
        total += static_cast<uint32_t>(n);
    }
    done = total;
    return {};
}

SharedFsStatus SharedFsFile::TruncateAt(uint64_t position) {
    const auto* state = static_cast<FileState*>(handle_);
    if (state == nullptr) return SharedStatus(SharedFsError::kInvalidHandle, EBADF);
    if (ftruncate(state->fd, static_cast<off_t>(position)) != 0) {
        return IoStatus(errno, SharedFsError::kWriteFault);
    }
    return {};
}

SharedFsStatus SharedFsFile::Info(SharedFsInfo& info) {
    const auto* state = static_cast<FileState*>(handle_);
    if (state == nullptr) return SharedStatus(SharedFsError::kInvalidHandle, EBADF);
    return DescribeSharedFd(state->fd, info);
}

}

}
