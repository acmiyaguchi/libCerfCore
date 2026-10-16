#include "host_crash.h"

#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>

#include <cerfcore/core/log.h>

#include <cstdint>
#include <mutex>

namespace host_platform {

namespace {

std::mutex g_dbghelp_mutex;
bool       g_dbghelp_inited = false;
HANDLE     g_crash_file     = INVALID_HANDLE_VALUE;

void EnsureDbgHelpInited_Locked() {
    if (g_dbghelp_inited) return;
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
    g_dbghelp_inited = true;
}

void EmergencySymbolizeAddr(const char* tag, void* addr) {
    char sym[512];
    if (CrashSymbolize(addr, sym, sizeof(sym))) {
        Log::Emergency("%s fault at %s (0x%p)\n", tag, sym, addr);
        return;
    }
    Log::Emergency("%s fault at 0x%p (sym-locked)\n", tag, addr);
}

void EmergencyWalkFaultingStack(const char* tag, CONTEXT* ctx_in) {
    if (!ctx_in) return;
    std::unique_lock<std::mutex> lk(g_dbghelp_mutex, std::try_to_lock);
    if (!lk.owns_lock()) {
        Log::Emergency("%s faulting-stack walk skipped (dbghelp locked)\n", tag);
        return;
    }
    EnsureDbgHelpInited_Locked();

    CONTEXT ctx = *ctx_in;
    STACKFRAME64 sf{};
    sf.AddrPC.Offset    = ctx.Eip; sf.AddrPC.Mode    = AddrModeFlat;
    sf.AddrFrame.Offset = ctx.Ebp; sf.AddrFrame.Mode = AddrModeFlat;
    sf.AddrStack.Offset = ctx.Esp; sf.AddrStack.Mode = AddrModeFlat;

    HANDLE proc = GetCurrentProcess();
    HANDLE thr  = GetCurrentThread();
    char line[512];
    char sym_buf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO* sym = (SYMBOL_INFO*)sym_buf;
    for (int i = 0; i < 40; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_I386, proc, thr, &sf, &ctx, NULL,
                         SymFunctionTableAccess64, SymGetModuleBase64, NULL))
            break;
        if (sf.AddrPC.Offset == 0) break;
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen   = 255;
        DWORD64 disp = 0;
        if (SymFromAddr(proc, sf.AddrPC.Offset, &disp, sym))
            wsprintfA(line, "%s   [%d] %s+0x%X\n", tag, i, sym->Name, (uint32_t)disp);
        else
            wsprintfA(line, "%s   [%d] 0x%p\n", tag, i,
                      (void*)(uintptr_t)sf.AddrPC.Offset);
        Log::Emergency("%s", line);
    }
}

LONG WINAPI CerfTopLevelExceptionFilter(EXCEPTION_POINTERS* ep) {
    LOG(Cerf, "this run ends here; the crash tail is in cerf.crash.log\n");
    Log::Close();
    Log::EmergencyStart();
    char recent_cautions[4096];
    Log::CopyRecentCautionsPostFreeze(recent_cautions,
                                      sizeof(recent_cautions));
    if (recent_cautions[0]) {
        Log::Emergency("[UNHANDLED] recent cautions:\n");
        CrashWrite(recent_cautions, (size_t)lstrlenA(recent_cautions));
    }
    if (ep && ep->ExceptionRecord) {
        EXCEPTION_RECORD* er = ep->ExceptionRecord;
        Log::Emergency("[UNHANDLED] code=0x%08X flags=0x%X at 0x%p\n",
                       (unsigned)er->ExceptionCode, (unsigned)er->ExceptionFlags,
                       er->ExceptionAddress);
        if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
            er->NumberParameters >= 2) {
            Log::Emergency("[UNHANDLED] access violation: %s 0x%p\n",
                           er->ExceptionInformation[0] ? "write to" : "read from",
                           (void*)er->ExceptionInformation[1]);
        }
        EmergencySymbolizeAddr("[UNHANDLED]", er->ExceptionAddress);
    }
    if (ep && ep->ContextRecord) {
        CONTEXT* c = ep->ContextRecord;
        Log::Emergency("[UNHANDLED] EIP=0x%p ESP=0x%p EBP=0x%p tid=%lu\n",
                       (void*)c->Eip, (void*)c->Esp, (void*)c->Ebp,
                       (unsigned long)GetCurrentThreadId());
    }
    Log::EmergencyDumpAllThreadStacks();
    EmergencyWalkFaultingStack("[UNHANDLED]", ep ? ep->ContextRecord : nullptr);
    Log::Close();
    ExitProcess((UINT)CERF_FATAL_RUNTIME_ERROR);
}

}

void CrashWrite(const char* buf, size_t len) {
    if (!buf || len == 0) return;
    DWORD w = 0;
    if (g_crash_file != INVALID_HANDLE_VALUE)
        WriteFile(g_crash_file, buf, (DWORD)len, &w, NULL);
    HANDLE herr = GetStdHandle(STD_ERROR_HANDLE);
    if (herr && herr != INVALID_HANDLE_VALUE)
        WriteFile(herr, buf, (DWORD)len, &w, NULL);
}

int CrashFormat(char* buf, size_t, const char* fmt, va_list ap) {
    return wvsprintfA(buf, fmt, ap);
}

void CrashFreezeOtherThreads() {
    DWORD my_tid = GetCurrentThreadId();
    DWORD my_pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != my_pid) continue;
            if (te.th32ThreadID == my_tid)       continue;
            HANDLE th = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
            if (th) {
                SuspendThread(th);
                CloseHandle(th);
            }
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
}

void CrashOpenLog() {
    wchar_t crash_path[MAX_PATH];
    const DWORD mod_len = GetModuleFileNameW(NULL, crash_path, MAX_PATH);
    int cut = 0;
    if (mod_len > 0 && mod_len < MAX_PATH) {
        for (int i = 0; i < (int)mod_len; ++i) {
            if (crash_path[i] == L'\\' || crash_path[i] == L'/') cut = i + 1;
        }
    }
    lstrcpynW(crash_path + cut, L"cerf.crash.log", MAX_PATH - cut);
    g_crash_file = CreateFileW(crash_path, GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    SYSTEMTIME st;
    GetLocalTime(&st);
    char hdr[256];
    int n = wsprintfA(hdr,
        "=== CERF CRASH %04u-%02u-%02u %02u:%02u:%02u.%03u (tid=%lu pid=%lu) ===\n",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
        st.wMilliseconds,
        (unsigned long)GetCurrentThreadId(),
        (unsigned long)GetCurrentProcessId());
    if (n > 0) CrashWrite(hdr, (size_t)n);
    HMODULE self = GetModuleHandleA(NULL);
    n = wsprintfA(hdr, "=== cerf.exe runtime base=0x%p (RVA = addr - base) ===\n",
                  (void*)self);
    if (n > 0) CrashWrite(hdr, (size_t)n);
}

void CrashDumpOtherThreads() {
    DWORD my_tid = GetCurrentThreadId();
    DWORD my_pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    Log::Emergency("\n=== All other threads' state at crash (frozen) ===\n");
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != my_pid) continue;
            if (te.th32ThreadID == my_tid)       continue;
            HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                                   FALSE, te.th32ThreadID);
            if (!th) continue;
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
            if (GetThreadContext(th, &ctx)) {
                Log::Emergency("--- tid=%lu  EIP=0x%p  ESP=0x%p  EBP=0x%p\n",
                               (unsigned long)te.th32ThreadID,
                               (void*)ctx.Eip, (void*)ctx.Esp, (void*)ctx.Ebp);
                if (ctx.Esp && !IsBadReadPtr((const void*)ctx.Esp, 16 * 4)) {
                    unsigned __int32* sp = (unsigned __int32*)ctx.Esp;
                    for (int i = 0; i < 16; i++) {
                        Log::Emergency("     [sp+%02X] 0x%08X\n",
                                       i * 4, sp[i]);
                    }
                }
            }
            CloseHandle(th);
        } while (Thread32Next(snap, &te));
    }
    Log::Emergency("=== end frozen thread dump ===\n\n");
    CloseHandle(snap);
}

void CrashPrintNativeStack(const char* tag) {
    void* frames[32];
    USHORT n = CaptureStackBackTrace(0, 32, frames, NULL);
    std::unique_lock<std::mutex> lk(g_dbghelp_mutex, std::try_to_lock);
    bool have_sym = lk.owns_lock();
    if (have_sym) EnsureDbgHelpInited_Locked();
    char line[512];
    char sym_buf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO* sym = (SYMBOL_INFO*)sym_buf;
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    for (USHORT i = 0; i < n; i++) {
        int ln = 0;
        if (have_sym) {
            DWORD64 disp = 0;
            if (SymFromAddr(GetCurrentProcess(), (DWORD64)frames[i], &disp, sym)) {
                ln = wsprintfA(line, "%s   [%u] %s+0x%X\n",
                               tag, i, sym->Name, (uint32_t)disp);
            } else {
                ln = wsprintfA(line, "%s   [%u] 0x%p\n", tag, i, frames[i]);
            }
        } else {
            ln = wsprintfA(line, "%s   [%u] 0x%p (sym-locked)\n",
                           tag, i, frames[i]);
        }
        if (ln > 0) CrashWrite(line, (size_t)ln);
    }
}

bool CrashSymbolize(const void* addr, char* out, size_t out_size) {
    if (!out || out_size == 0) return false;
    out[0] = '\0';

    std::unique_lock<std::mutex> lk(g_dbghelp_mutex, std::try_to_lock);
    if (!lk.owns_lock()) return false;
    EnsureDbgHelpInited_Locked();

    char         sym_buf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO* sym  = (SYMBOL_INFO*)sym_buf;
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen   = 255;
    DWORD64 disp      = 0;
    if (!SymFromAddr(GetCurrentProcess(), (DWORD64)addr, &disp, sym)) {
        return false;
    }

    char line[512];
    if (wsprintfA(line, "%s+0x%X", sym->Name, (uint32_t)disp) <= 0) return false;
    size_t n = 0;
    while (line[n] != '\0' && n + 1 < out_size) { out[n] = line[n]; ++n; }
    out[n] = '\0';
    return true;
}

void CrashInstallUnhandledFilter() {
    SetUnhandledExceptionFilter(CerfTopLevelExceptionFilter);
}

}
