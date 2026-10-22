#include <cerfcore/platform/host_file.h>

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <linux/falloc.h>
#elif defined(__APPLE__)
#include <sys/mount.h>
#endif

#include "host_fd_handle.h"
#include "host_unimplemented.h"

namespace cerfcore {

namespace host_platform {

namespace {

int OpenFlags(FileMode mode) {
    switch (mode) {
        case FileMode::kReadExisting:      return O_RDONLY;
        case FileMode::kWriteNew:          return O_WRONLY | O_CREAT | O_TRUNC;
        case FileMode::kWriteNewShared:    return O_WRONLY | O_CREAT | O_TRUNC;
        case FileMode::kReadWriteAlways:   return O_RDWR | O_CREAT;
        case FileMode::kReadWriteExisting: return O_RDWR;
    }
    NoHostImplementation("HostFile::Open mode");
}

std::string ParentDirectory(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return ".";
    return slash == 0 ? std::string("/") : path.substr(0, slash);
}

[[maybe_unused]] bool WriteZeros(int fd, uint64_t begin, uint64_t end) {
    static constexpr uint32_t kChunk = 65536u;
    static const uint8_t      kZeros[kChunk] = {};
    while (begin < end) {
        const uint64_t left = end - begin;
        const size_t   want = static_cast<size_t>(left < kChunk ? left : kChunk);
        const ssize_t  n    = ::pwrite(fd, kZeros, want, static_cast<off_t>(begin));
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        begin += static_cast<uint64_t>(n);
    }
    return true;
}

}

HostFile::~HostFile() { Close(); }

bool HostFile::Open(const std::string& path, FileMode mode) {
    const int fd = ::open(path.c_str(), OpenFlags(mode) | O_CLOEXEC, 0666);
    if (fd < 0) return false;
    handle_ = HandleOf(fd);
    return true;
}

void HostFile::Close() {
    if (handle_) {
        ::close(FdOf(handle_));
        handle_ = nullptr;
    }
}

bool HostFile::Size(uint64_t& size) const {
    struct stat st {};
    if (::fstat(FdOf(handle_), &st) != 0) return false;
    size = static_cast<uint64_t>(st.st_size);
    return true;
}

bool HostFile::SeekTo(uint64_t offset) {
    return ::lseek(FdOf(handle_), static_cast<off_t>(offset), SEEK_SET) >= 0;
}

bool HostFile::SeekToEnd() { return ::lseek(FdOf(handle_), 0, SEEK_END) >= 0; }

bool HostFile::Read(void* dst, uint32_t length, uint32_t& done) {
    done = 0;
    while (done < length) {
        const ssize_t n = ::read(FdOf(handle_), static_cast<uint8_t*>(dst) + done, length - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) break;
        done += static_cast<uint32_t>(n);
    }
    return true;
}

bool HostFile::Write(const void* src, uint32_t length, uint32_t& done) {
    done = 0;
    while (done < length) {
        const ssize_t n =
            ::write(FdOf(handle_), static_cast<const uint8_t*>(src) + done, length - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        done += static_cast<uint32_t>(n);
    }
    return true;
}

void HostFile::MarkSparse() {}

bool HostFile::ZeroRange(uint64_t begin, uint64_t end) {
    if (end <= begin) return true;
#if defined(__linux__)
    const int fd = FdOf(handle_);
    for (;;) {
        if (::fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                        static_cast<off_t>(begin), static_cast<off_t>(end - begin)) == 0)
            return true;
        if (errno == EINTR) continue;
        if (errno != EOPNOTSUPP) return false;
        break;
    }
    uint64_t size = 0;
    if (!Size(size)) return false;
    return WriteZeros(fd, begin, end < size ? end : size);
#elif defined(__APPLE__)
    const int fd   = FdOf(handle_);
    uint64_t  size = 0;
    if (!Size(size)) return false;
    const uint64_t stop = end < size ? end : size;
    if (begin >= stop) return true;
    struct statfs fs {};
    if (::fstatfs(fd, &fs) != 0 || fs.f_bsize == 0u) return WriteZeros(fd, begin, stop);
    const uint64_t block = fs.f_bsize;
    const uint64_t first = (begin + block - 1u) / block * block;
    const uint64_t last  = stop / block * block;
    if (first >= last) return WriteZeros(fd, begin, stop);
    fpunchhole_t hole{};
    hole.fp_offset = static_cast<off_t>(first);
    hole.fp_length = static_cast<off_t>(last - first);
    for (;;) {
        if (::fcntl(fd, F_PUNCHHOLE, &hole) == 0) {
            return WriteZeros(fd, begin, first) && WriteZeros(fd, last, stop);
        }
        if (errno != EINTR) break;
    }
    return WriteZeros(fd, begin, stop);
#else
    NoHostImplementation("HostFile::ZeroRange");
#endif
}

DeleteResult DeleteHostFile(const std::string& path, uint32_t& error) {
    if (::unlink(path.c_str()) == 0) {
        error = 0;
        return DeleteResult::kDeleted;
    }
    error = static_cast<uint32_t>(errno);
    if (errno == ENOENT || errno == ENOTDIR) return DeleteResult::kMissing;
    return DeleteResult::kFailed;
}

bool ReplaceHostFile(const std::string& from, const std::string& to, bool write_through) {
    if (::rename(from.c_str(), to.c_str()) != 0) return false;
    if (!write_through) return true;
    const int dir = ::open(ParentDirectory(to).c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir < 0) return false;
    const bool synced = ::fsync(dir) == 0;
    ::close(dir);
    return synced;
}

bool HostFileExists(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && !S_ISDIR(st.st_mode);
}

bool HostPathExists(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0;
}

}

}
