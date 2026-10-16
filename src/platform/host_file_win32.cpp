#include <cerfcore/platform/host_file.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>

#include <cerfcore/platform/wide_text_win32.h>

namespace cerfcore {

namespace host_platform {

HostFile::~HostFile() {
    Close();
}

bool HostFile::Open(const std::string& path, FileMode mode) {
    DWORD access      = GENERIC_READ;
    DWORD share       = FILE_SHARE_READ;
    DWORD disposition = OPEN_EXISTING;
    switch (mode) {
        case FileMode::kReadExisting:
            break;
        case FileMode::kWriteNew:
            access      = GENERIC_WRITE;
            share       = 0;
            disposition = CREATE_ALWAYS;
            break;
        case FileMode::kWriteNewShared:
            access      = GENERIC_WRITE;
            share       = FILE_SHARE_READ | FILE_SHARE_WRITE;
            disposition = CREATE_ALWAYS;
            break;
        case FileMode::kReadWriteAlways:
            access      = GENERIC_READ | GENERIC_WRITE;
            disposition = OPEN_ALWAYS;
            break;
        case FileMode::kReadWriteExisting:
            access      = GENERIC_READ | GENERIC_WRITE;
            break;
    }
    HANDLE h = CreateFileW(Utf8ToWide(path.c_str()).c_str(), access, share, nullptr,
                           disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    handle_ = h;
    return true;
}

void HostFile::Close() {
    if (handle_) {
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
}

bool HostFile::Size(uint64_t& size) const {
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(static_cast<HANDLE>(handle_), &sz)) return false;
    size = static_cast<uint64_t>(sz.QuadPart);
    return true;
}

bool HostFile::SeekTo(uint64_t offset) {
    LARGE_INTEGER li;
    li.QuadPart = static_cast<LONGLONG>(offset);
    return SetFilePointerEx(static_cast<HANDLE>(handle_), li, nullptr, FILE_BEGIN) != 0;
}

bool HostFile::SeekToEnd() {
    LARGE_INTEGER zero{};
    return SetFilePointerEx(static_cast<HANDLE>(handle_), zero, nullptr, FILE_END) != 0;
}

bool HostFile::Read(void* dst, uint32_t length, uint32_t& done) {
    DWORD got = 0;
    const BOOL ok = ReadFile(static_cast<HANDLE>(handle_), dst, length, &got, nullptr);
    done = got;
    return ok != 0;
}

bool HostFile::Write(const void* src, uint32_t length, uint32_t& done) {
    DWORD put = 0;
    const BOOL ok = WriteFile(static_cast<HANDLE>(handle_), src, length, &put, nullptr);
    done = put;
    return ok != 0;
}

void HostFile::MarkSparse() {
    DWORD junk = 0;
    DeviceIoControl(static_cast<HANDLE>(handle_), FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0,
                    &junk, nullptr);
}

bool HostFile::ZeroRange(uint64_t begin, uint64_t end) {
    FILE_ZERO_DATA_INFORMATION z{};
    z.FileOffset.QuadPart      = static_cast<LONGLONG>(begin);
    z.BeyondFinalZero.QuadPart = static_cast<LONGLONG>(end);
    DWORD junk = 0;
    return DeviceIoControl(static_cast<HANDLE>(handle_), FSCTL_SET_ZERO_DATA, &z, sizeof(z),
                           nullptr, 0, &junk, nullptr) != 0;
}

DeleteResult DeleteHostFile(const std::string& path, uint32_t& error) {
    const std::wstring wide = Utf8ToWide(path.c_str());
    if (DeleteFileW(wide.c_str())) {
        error = 0;
        return DeleteResult::kDeleted;
    }
    error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        return DeleteResult::kMissing;
    return DeleteResult::kFailed;
}

bool ReplaceHostFile(const std::string& from, const std::string& to, bool write_through) {
    const DWORD flags = MOVEFILE_REPLACE_EXISTING | (write_through ? MOVEFILE_WRITE_THROUGH : 0u);
    return MoveFileExW(Utf8ToWide(from.c_str()).c_str(), Utf8ToWide(to.c_str()).c_str(),
                       flags) != 0;
}

bool HostFileExists(const std::string& path) {
    const DWORD a = GetFileAttributesW(Utf8ToWide(path.c_str()).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool HostPathExists(const std::string& path) {
    return GetFileAttributesW(Utf8ToWide(path.c_str()).c_str()) != INVALID_FILE_ATTRIBUTES;
}

}

}
