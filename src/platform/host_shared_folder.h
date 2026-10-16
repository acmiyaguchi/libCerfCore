#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace host_platform {

enum class SharedFsError : uint8_t {
    kNone,
    kFileNotFound,
    kPathNotFound,
    kTooManyOpenFiles,
    kAccessDenied,
    kDirNotEmpty,
    kInvalidHandle,
    kNoMoreFiles,
    kWriteProtect,
    kWriteFault,
    kReadFault,
    kSharingViolation,
    kLockViolation,
    kDiskFull,
    kFileExists,
    kAlreadyExists,
    kInvalidName,
    kUnrepresentableTime,
    kUnmapped,
};

struct SharedFsStatus {
    SharedFsError error  = SharedFsError::kNone;
    uint32_t      native = 0;

    bool Ok() const { return error == SharedFsError::kNone; }
};

/* Linux msdos_fs.h ATTR_RO, ATTR_HIDDEN, ATTR_SYS, ATTR_DIR, ATTR_ARCH. */
enum SharedFsAttribute : uint8_t {
    kSharedReadOnly  = 0x01,
    kSharedHidden    = 0x02,
    kSharedSystem    = 0x04,
    kSharedDirectory = 0x10,
    kSharedArchive   = 0x20,
};

struct SharedFsInfo {
    uint8_t  attributes  = 0;
    uint64_t size        = 0;
    uint32_t write_time  = 0;
    uint32_t create_time = 0;
};

enum class SharedPathResult { kResolved, kRootUnavailable, kInvalidName };

SharedPathResult ResolveSharedPath(const std::string& root, std::u16string_view guest_name,
                                   std::u16string& out);

SharedFsStatus CreateSharedDirectory(const std::u16string& path);
SharedFsStatus RemoveSharedDirectory(const std::u16string& path);
SharedFsStatus DeleteSharedFile(const std::u16string& path);
SharedFsStatus MoveSharedFile(const std::u16string& from, const std::u16string& to);
SharedFsStatus SetSharedWriteTime(const std::u16string& path, uint32_t dos_time);
SharedFsStatus SetSharedAttributes(const std::u16string& path, uint8_t attributes);
SharedFsStatus QuerySharedInfo(const std::u16string& path, SharedFsInfo& info);
SharedFsStatus QuerySharedSpace(const std::string& root, uint64_t& total, uint64_t& available);
SharedFsStatus CreateNewSharedFile(const std::u16string& path);

class SharedFsFind {
public:
    SharedFsFind() = default;
    ~SharedFsFind();
    SharedFsFind(const SharedFsFind&)            = delete;
    SharedFsFind& operator=(const SharedFsFind&) = delete;

    SharedFsStatus First(const std::u16string& pattern, SharedFsInfo& info, std::u16string& name);
    SharedFsStatus Next(SharedFsInfo& info, std::u16string& name);

private:
    void* handle_ = nullptr;
};

enum class SharedFileAccess { kRead, kWrite, kReadWrite };
enum class SharedFileShare { kDenyReadWrite, kDenyWrite, kDenyRead, kDenyNone };

class SharedFsFile {
public:
    SharedFsFile() = default;
    ~SharedFsFile();
    SharedFsFile(const SharedFsFile&)            = delete;
    SharedFsFile& operator=(const SharedFsFile&) = delete;

    SharedFsStatus Open(const std::u16string& path, SharedFileAccess access, SharedFileShare share);
    SharedFsStatus ReadAt(uint64_t position, void* buf, uint32_t size, uint32_t& done);
    SharedFsStatus WriteAt(uint64_t position, const void* buf, uint32_t size, uint32_t& done);
    SharedFsStatus TruncateAt(uint64_t position);
    SharedFsStatus Info(SharedFsInfo& info);

private:
    void* handle_ = nullptr;
};

}
