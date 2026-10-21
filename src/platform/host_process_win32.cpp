#include <cerfcore/platform/host_process.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cerfcore/platform/wide_text_win32.h>

namespace cerfcore {

namespace host_platform {

namespace {

bool Usable(HANDLE h) { return h != nullptr && h != INVALID_HANDLE_VALUE; }

bool WriteAll(HANDLE h, const char* buf, size_t len) {
    if (!Usable(h)) return false;
    size_t done = 0;
    while (done < len) {
        DWORD written = 0;
        if (!WriteFile(h, buf + done, (DWORD)(len - done), &written, nullptr) || written == 0)
            return false;
        done += written;
    }
    return true;
}

}

std::string ExecutablePath() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = ::GetModuleFileNameW(NULL, &buf[0], (DWORD)buf.size());
        if (n == 0) return {};
        if (n < buf.size()) {
            buf.resize(n);
            return WideToUtf8(buf);
        }
        if (buf.size() >= 32768) return {};
        buf.resize(buf.size() * 2);
    }
}

uint32_t CurrentProcessId() { return static_cast<uint32_t>(::GetCurrentProcessId()); }

bool StdoutAvailable() { return Usable(GetStdHandle(STD_OUTPUT_HANDLE)); }

bool WriteStdout(const char* buf, size_t len) {
    return WriteAll(GetStdHandle(STD_OUTPUT_HANDLE), buf, len);
}

bool WriteStderr(const char* buf, size_t len) {
    return WriteAll(GetStdHandle(STD_ERROR_HANDLE), buf, len);
}

void ExitProcessNow(int code) {
    ExitProcess((UINT)code);
}

}

}
