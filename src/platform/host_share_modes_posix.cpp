#include "host_share_modes.h"

#include "host_shared_entry.h"

#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace cerfcore {

namespace host_platform {

namespace {

enum Marker : int {
    kOpenMarker,
    kReadMarker,
    kWriteMarker,
    kDenyReadMarker,
    kDenyWriteMarker,
};

struct MarkerUse {
    bool   on;
    Marker marker;
};

constexpr off_t kMarkerBase = 0x7FFFFFFFFFFFFF00;

struct flock MarkerLock(short type, Marker marker) {
    struct flock fl {};
    fl.l_type   = type;
    fl.l_whence = SEEK_SET;
    fl.l_start  = kMarkerBase + marker;
    fl.l_len    = 1;
    fl.l_pid    = 0;
    return fl;
}

int LockCall(int fd, int cmd, struct flock& fl) {
    while (fcntl(fd, cmd, &fl) != 0) {
        if (errno != EINTR) return errno;
    }
    return 0;
}

int HeldElsewhere(int fd, Marker marker, bool& held) {
    struct flock fl  = MarkerLock(F_WRLCK, marker);
    const int    err = LockCall(fd, F_OFD_GETLK, fl);
    held             = err == 0 && fl.l_type != F_UNLCK;
    return err;
}

int Take(int fd, Marker marker) {
    struct flock fl = MarkerLock(F_RDLCK, marker);
    return LockCall(fd, F_OFD_SETLK, fl);
}

SharedFsStatus TakeStatus(int err, const std::string& path) {
    return err == EAGAIN ? SharedStatus(SharedFsError::kSharingViolation, err) : SharedErrno(err, path);
}

SharedFsStatus ProbeOpen(int fd, const std::string& path, bool& held) {
    const int err = HeldElsewhere(fd, kOpenMarker, held);
    return err == 0 ? SharedFsStatus{} : SharedErrno(err, path);
}

SharedFsStatus ProbeTree(int dir_fd, const std::string& path, bool& held) {
    SharedFsStatus st = ProbeOpen(dir_fd, path, held);
    if (!st.Ok() || held) {
        close(dir_fd);
        return st;
    }
    DIR* d = fdopendir(dir_fd);
    if (d == nullptr) {
        const int err = errno;
        close(dir_fd);
        return SharedErrno(err, path);
    }
    while (const dirent* e = readdir(d)) {
        if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
        const std::string child = JoinSharedPath(path, e->d_name);
        struct stat       sb {};
        if (fstatat(dirfd(d), e->d_name, &sb, AT_SYMLINK_NOFOLLOW) != 0) {
            st = SharedErrno(errno, child);
            break;
        }
        const bool link = S_ISLNK(sb.st_mode);
        if (link && fstatat(dirfd(d), e->d_name, &sb, 0) != 0) {
            if (errno == ENOENT) continue;
            st = SharedErrno(errno, child);
            break;
        }
        if (S_ISDIR(sb.st_mode)) {
            if (link) continue;
            const int fd = openat(dirfd(d), e->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            st = fd < 0 ? SharedErrno(errno, child) : ProbeTree(fd, child, held);
        } else if (S_ISREG(sb.st_mode)) {
            const int fd = openat(dirfd(d), e->d_name, O_RDONLY | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
            if (fd < 0) {
                st = SharedErrno(errno, child);
            } else {
                st = ProbeOpen(fd, child, held);
                close(fd);
            }
        }
        if (!st.Ok() || held) break;
    }
    closedir(d);
    return st;
}

SharedFsStatus CheckFileFree(const std::string& path) {
    const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
    if (fd < 0) return SharedErrno(errno, path);
    bool                 held = false;
    const SharedFsStatus st   = ProbeOpen(fd, path, held);
    close(fd);
    if (!st.Ok()) return st;
    return held ? SharedStatus(SharedFsError::kSharingViolation, EBUSY) : SharedFsStatus{};
}

SharedFsStatus CheckTreeFree(const std::string& dir) {
    const int fd = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return SharedErrno(errno, dir);
    bool                 held = false;
    const SharedFsStatus st   = ProbeTree(fd, dir, held);
    if (!st.Ok()) return st;
    return held ? SharedStatus(SharedFsError::kAccessDenied, EBUSY) : SharedFsStatus{};
}

}

SharedFsStatus ClaimShareMode(int lock_fd, const std::string& path, const ShareClaim& claim) {
    const MarkerUse takes[] = {{true, kOpenMarker},
                               {claim.read, kReadMarker},
                               {claim.write, kWriteMarker},
                               {!claim.share_read, kDenyReadMarker},
                               {!claim.share_write, kDenyWriteMarker}};
    for (const MarkerUse& t : takes) {
        if (!t.on) continue;
        if (const int err = Take(lock_fd, t.marker); err != 0) return TakeStatus(err, path);
    }
    const MarkerUse conflicts[] = {{claim.read, kDenyReadMarker},
                                   {claim.write, kDenyWriteMarker},
                                   {!claim.share_read, kReadMarker},
                                   {!claim.share_write, kWriteMarker}};
    for (const MarkerUse& c : conflicts) {
        if (!c.on) continue;
        bool held = false;
        if (const int err = HeldElsewhere(lock_fd, c.marker, held); err != 0) return SharedErrno(err, path);
        if (held) return SharedStatus(SharedFsError::kSharingViolation, EAGAIN);
    }
    return {};
}

SharedFsStatus HoldSharedDirectory(const std::string& dir, int& fd) {
    fd = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return SharedErrno(errno, dir);
    if (const int err = Take(fd, kOpenMarker); err != 0) {
        close(fd);
        fd = -1;
        return TakeStatus(err, dir);
    }
    return {};
}

SharedFsStatus CheckSharedEntryFree(const std::string& path) {
    struct stat sb {};
    if (stat(path.c_str(), &sb) != 0) return SharedErrno(errno, path);
    if (S_ISDIR(sb.st_mode)) return CheckTreeFree(path);
    if (S_ISREG(sb.st_mode)) return CheckFileFree(path);
    return {};
}

}

}
