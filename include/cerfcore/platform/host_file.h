#pragma once

#include <cstdint>
#include <string>

namespace host_platform {

enum class FileMode : uint8_t {
    kReadExisting,
    kWriteNew,
    kWriteNewShared,
    kReadWriteAlways,
    kReadWriteExisting,
};

class HostFile {
public:
    HostFile() = default;
    ~HostFile();
    HostFile(const HostFile&)            = delete;
    HostFile& operator=(const HostFile&) = delete;

    bool Open(const std::string& path, FileMode mode);
    void Close();
    bool IsOpen() const { return handle_ != nullptr; }

    bool Size(uint64_t& size) const;
    bool SeekTo(uint64_t offset);
    bool SeekToEnd();
    bool Read(void* dst, uint32_t length, uint32_t& done);
    bool Write(const void* src, uint32_t length, uint32_t& done);
    void MarkSparse();
    bool ZeroRange(uint64_t begin, uint64_t end);

private:
    void* handle_ = nullptr;
};

enum class DeleteResult : uint8_t {
    kDeleted,
    kMissing,
    kFailed,
};

DeleteResult DeleteHostFile(const std::string& path, uint32_t& error);
bool         ReplaceHostFile(const std::string& from, const std::string& to, bool write_through);
bool         HostFileExists(const std::string& path);
bool         HostPathExists(const std::string& path);

}
