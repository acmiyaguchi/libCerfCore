#include "host_shared_folder.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cerfcore/platform/wide_text_win32.h>

#include <cwchar>

namespace cerfcore {

namespace host_platform {

static_assert(sizeof(wchar_t) == sizeof(char16_t));

namespace {

const wchar_t* W(const std::u16string& s) { return reinterpret_cast<const wchar_t*>(s.c_str()); }
HANDLE H(void* p) { return static_cast<HANDLE>(p); }

SharedFsStatus FromNative(DWORD err) {
    SharedFsStatus st;
    st.native = err;
    switch (err) {
        case ERROR_FILE_NOT_FOUND:       st.error = SharedFsError::kFileNotFound;     break;
        case ERROR_PATH_NOT_FOUND:       st.error = SharedFsError::kPathNotFound;     break;
        case ERROR_TOO_MANY_OPEN_FILES:  st.error = SharedFsError::kTooManyOpenFiles; break;
        case ERROR_ACCESS_DENIED:        st.error = SharedFsError::kAccessDenied;     break;
        case ERROR_DIR_NOT_EMPTY:        st.error = SharedFsError::kDirNotEmpty;      break;
        case ERROR_INVALID_HANDLE:       st.error = SharedFsError::kInvalidHandle;    break;
        case ERROR_NO_MORE_FILES:        st.error = SharedFsError::kNoMoreFiles;      break;
        case ERROR_WRITE_PROTECT:        st.error = SharedFsError::kWriteProtect;     break;
        case ERROR_WRITE_FAULT:          st.error = SharedFsError::kWriteFault;       break;
        case ERROR_READ_FAULT:           st.error = SharedFsError::kReadFault;        break;
        case ERROR_SHARING_VIOLATION:    st.error = SharedFsError::kSharingViolation; break;
        case ERROR_LOCK_VIOLATION:       st.error = SharedFsError::kLockViolation;    break;
        case ERROR_HANDLE_DISK_FULL:     st.error = SharedFsError::kDiskFull;         break;
        case ERROR_DISK_FULL:            st.error = SharedFsError::kDiskFull;         break;
        case ERROR_FILE_EXISTS:          st.error = SharedFsError::kFileExists;       break;
        case ERROR_ALREADY_EXISTS:       st.error = SharedFsError::kAlreadyExists;    break;
        case ERROR_INVALID_NAME:         st.error = SharedFsError::kInvalidName;      break;
        case ERROR_BAD_PATHNAME:         st.error = SharedFsError::kInvalidName;      break;
        case ERROR_FILENAME_EXCED_RANGE: st.error = SharedFsError::kInvalidName;      break;
        default:                         st.error = SharedFsError::kUnmapped;         break;
    }
    return st;
}

SharedFsStatus LastError() { return FromNative(GetLastError()); }

uint32_t ToDosTime(const FILETIME& ft) {
    FILETIME local;
    WORD dos_date, dos_time;
    if (FileTimeToLocalFileTime(&ft, &local) &&
        FileTimeToDosDateTime(&local, &dos_date, &dos_time)) {
        return MAKELONG(dos_time, dos_date);
    }
    return 0;
}

bool FromDosTime(uint32_t dos_datetime, FILETIME& out) {
    FILETIME local;
    if (!DosDateTimeToFileTime(HIWORD(dos_datetime), LOWORD(dos_datetime), &local))
        return false;
    return LocalFileTimeToFileTime(&local, &out) != 0;
}

uint8_t ToSharedAttributes(DWORD attrs) {
    uint8_t out = 0;
    if (attrs & FILE_ATTRIBUTE_READONLY)  out |= kSharedReadOnly;
    if (attrs & FILE_ATTRIBUTE_HIDDEN)    out |= kSharedHidden;
    if (attrs & FILE_ATTRIBUTE_SYSTEM)    out |= kSharedSystem;
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) out |= kSharedDirectory;
    if (attrs & FILE_ATTRIBUTE_ARCHIVE)   out |= kSharedArchive;
    return out;
}

void FillInfo(SharedFsInfo& info, DWORD attrs, DWORD size_high, DWORD size_low,
              const FILETIME& write_time, const FILETIME& create_time) {
    info.attributes  = ToSharedAttributes(attrs);
    info.size        = (uint64_t(size_high) << 32) | size_low;
    info.write_time  = ToDosTime(write_time);
    info.create_time = ToDosTime(create_time);
}

void FillFromFindData(const WIN32_FIND_DATAW& fd, SharedFsInfo& info, std::u16string& name) {
    FillInfo(info, fd.dwFileAttributes, fd.nFileSizeHigh, fd.nFileSizeLow,
             fd.ftLastWriteTime, fd.ftCreationTime);
    name.assign(reinterpret_cast<const char16_t*>(fd.cFileName), wcslen(fd.cFileName));
}

}

SharedPathResult ResolveSharedPath(const std::string& root_utf8, std::u16string_view guest_name,
                                   std::u16string& out) {
    std::wstring root = Utf8ToWide(root_utf8.c_str());
    wchar_t canon_root[MAX_PATH];
    const DWORD rn = GetFullPathNameW(root.c_str(), MAX_PATH, canon_root, nullptr);
    if (rn == 0 || rn >= MAX_PATH) return SharedPathResult::kRootUnavailable;
    root.assign(canon_root);
    while (!root.empty() && (root.back() == L'\\' || root.back() == L'/'))
        root.pop_back();
    if (root.empty()) return SharedPathResult::kRootUnavailable;

    std::wstring joined = root;
    joined.append(reinterpret_cast<const wchar_t*>(guest_name.data()), guest_name.size());

    wchar_t full[MAX_PATH];
    const DWORD n = GetFullPathNameW(joined.c_str(), MAX_PATH, full, nullptr);
    if (n == 0 || n >= MAX_PATH) return SharedPathResult::kInvalidName;

    const size_t rlen = root.size();
    if (_wcsnicmp(full, root.c_str(), rlen) != 0) return SharedPathResult::kInvalidName;
    if (full[rlen] != L'\\' && full[rlen] != L'/' && full[rlen] != L'\0')
        return SharedPathResult::kInvalidName;

    out.assign(reinterpret_cast<const char16_t*>(full), wcslen(full));
    return SharedPathResult::kResolved;
}

SharedFsStatus CreateSharedDirectory(const std::u16string& path) {
    if (!CreateDirectoryW(W(path), nullptr)) return LastError();
    return {};
}

SharedFsStatus RemoveSharedDirectory(const std::u16string& path) {
    if (!RemoveDirectoryW(W(path))) return LastError();
    return {};
}

SharedFsStatus DeleteSharedFile(const std::u16string& path) {
    if (!DeleteFileW(W(path))) return LastError();
    return {};
}

SharedFsStatus MoveSharedFile(const std::u16string& from, const std::u16string& to) {
    if (!MoveFileW(W(from), W(to))) return LastError();
    return {};
}

SharedFsStatus SetSharedWriteTime(const std::u16string& path, uint32_t dos_time) {
    FILETIME write_ft;
    if (!FromDosTime(dos_time, write_ft)) {
        SharedFsStatus st;
        st.error = SharedFsError::kUnrepresentableTime;
        return st;
    }
    HANDLE h = CreateFileW(W(path), FILE_WRITE_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return LastError();
    const BOOL ok = SetFileTime(h, nullptr, nullptr, &write_ft);
    const DWORD err = ok ? 0 : GetLastError();
    CloseHandle(h);
    if (!ok) return FromNative(err);
    return {};
}

SharedFsStatus SetSharedAttributes(const std::u16string& path, uint8_t attributes) {
    DWORD attrs = 0;
    if (attributes & kSharedReadOnly) attrs |= FILE_ATTRIBUTE_READONLY;
    if (attributes & kSharedHidden)   attrs |= FILE_ATTRIBUTE_HIDDEN;
    if (attributes & kSharedSystem)   attrs |= FILE_ATTRIBUTE_SYSTEM;
    if (attributes & kSharedArchive)  attrs |= FILE_ATTRIBUTE_ARCHIVE;
    if (attrs == 0) attrs = FILE_ATTRIBUTE_NORMAL;
    if (!SetFileAttributesW(W(path), attrs)) return LastError();
    return {};
}

SharedFsStatus QuerySharedInfo(const std::u16string& path, SharedFsInfo& info) {
    WIN32_FILE_ATTRIBUTE_DATA ad;
    if (!GetFileAttributesExW(W(path), GetFileExInfoStandard, &ad)) return LastError();
    FillInfo(info, ad.dwFileAttributes, ad.nFileSizeHigh, ad.nFileSizeLow,
             ad.ftLastWriteTime, ad.ftCreationTime);
    return {};
}

SharedFsStatus QuerySharedSpace(const std::string& root, uint64_t& total, uint64_t& available) {
    ULARGE_INTEGER avail, tot;
    if (!GetDiskFreeSpaceExW(Utf8ToWide(root.c_str()).c_str(), &avail, &tot, nullptr))
        return LastError();
    total     = tot.QuadPart;
    available = avail.QuadPart;
    return {};
}

SharedFsStatus CreateNewSharedFile(const std::u16string& path) {
    HANDLE h = CreateFileW(W(path), GENERIC_WRITE, 0, nullptr,
                           CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return LastError();
    CloseHandle(h);
    return {};
}

SharedFsFind::~SharedFsFind() {
    if (handle_) FindClose(H(handle_));
}

SharedFsStatus SharedFsFind::First(const std::u16string& pattern, SharedFsInfo& info,
                                   std::u16string& name) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(W(pattern), &fd);
    if (h == INVALID_HANDLE_VALUE) return LastError();
    handle_ = h;
    FillFromFindData(fd, info, name);
    return {};
}

SharedFsStatus SharedFsFind::Next(SharedFsInfo& info, std::u16string& name) {
    WIN32_FIND_DATAW fd;
    if (!FindNextFileW(H(handle_), &fd)) return LastError();
    FillFromFindData(fd, info, name);
    return {};
}

SharedFsFile::~SharedFsFile() {
    if (handle_) CloseHandle(H(handle_));
}

SharedFsStatus SharedFsFile::Open(const std::u16string& path, SharedFileAccess access,
                                  SharedFileShare share) {
    DWORD acc = 0;
    switch (access) {
        case SharedFileAccess::kRead:      acc = GENERIC_READ; break;
        case SharedFileAccess::kWrite:     acc = GENERIC_WRITE; break;
        case SharedFileAccess::kReadWrite: acc = GENERIC_READ | GENERIC_WRITE; break;
    }
    DWORD shr = 0;
    switch (share) {
        case SharedFileShare::kDenyReadWrite: shr = 0; break;
        case SharedFileShare::kDenyWrite:     shr = FILE_SHARE_READ; break;
        case SharedFileShare::kDenyRead:      shr = FILE_SHARE_WRITE; break;
        case SharedFileShare::kDenyNone:      shr = FILE_SHARE_READ | FILE_SHARE_WRITE; break;
    }
    HANDLE h = CreateFileW(W(path), acc, shr, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return LastError();
    handle_ = h;
    return {};
}

SharedFsStatus SharedFsFile::ReadAt(uint64_t position, void* buf, uint32_t size, uint32_t& done) {
    LARGE_INTEGER pos;
    pos.QuadPart = static_cast<LONGLONG>(position);
    if (!SetFilePointerEx(H(handle_), pos, nullptr, FILE_BEGIN)) return LastError();
    DWORD n = 0;
    if (!ReadFile(H(handle_), buf, size, &n, nullptr)) return LastError();
    done = n;
    return {};
}

SharedFsStatus SharedFsFile::WriteAt(uint64_t position, const void* buf, uint32_t size,
                                     uint32_t& done) {
    LARGE_INTEGER pos;
    pos.QuadPart = static_cast<LONGLONG>(position);
    if (!SetFilePointerEx(H(handle_), pos, nullptr, FILE_BEGIN)) return LastError();
    DWORD n = 0;
    if (!WriteFile(H(handle_), buf, size, &n, nullptr)) return LastError();
    done = n;
    return {};
}

SharedFsStatus SharedFsFile::TruncateAt(uint64_t position) {
    LARGE_INTEGER pos;
    pos.QuadPart = static_cast<LONGLONG>(position);
    if (!SetFilePointerEx(H(handle_), pos, nullptr, FILE_BEGIN)) return LastError();
    if (!SetEndOfFile(H(handle_))) return LastError();
    return {};
}

SharedFsStatus SharedFsFile::Info(SharedFsInfo& info) {
    BY_HANDLE_FILE_INFORMATION fi;
    if (!GetFileInformationByHandle(H(handle_), &fi)) return LastError();
    FillInfo(info, fi.dwFileAttributes, fi.nFileSizeHigh, fi.nFileSizeLow,
             fi.ftLastWriteTime, fi.ftCreationTime);
    return {};
}

}

}
