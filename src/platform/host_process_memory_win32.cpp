#include "host_process_memory.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <cstring>

namespace host_platform {

ProcessMemoryUsage QueryProcessMemory() {
    ProcessMemoryUsage out;
    PROCESS_MEMORY_COUNTERS_EX pmc;
    memset(&pmc, 0, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    if (::GetProcessMemoryInfo(::GetCurrentProcess(),
                               reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                               sizeof(pmc))) {
        out.private_bytes = pmc.PrivateUsage;
        out.working_set   = pmc.WorkingSetSize;
    }
    return out;
}

ThreadStackUsage MeasureThreadStacks() {
    ThreadStackUsage out;
    const DWORD our_pid = ::GetCurrentProcessId();

    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;

    THREADENTRY32 te;
    memset(&te, 0, sizeof(te));
    te.dwSize = sizeof(te);
    if (!::Thread32First(snap, &te)) {
        ::CloseHandle(snap);
        return out;
    }

    do {
        if (te.th32OwnerProcessID != our_pid) continue;
        out.count++;

        HANDLE ht = ::OpenThread(
            THREAD_QUERY_LIMITED_INFORMATION | THREAD_GET_CONTEXT,
            FALSE, te.th32ThreadID);
        if (!ht) continue;

        CONTEXT ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.ContextFlags = CONTEXT_CONTROL;
        if (::GetThreadContext(ht, &ctx)) {
            MEMORY_BASIC_INFORMATION mbi;
            memset(&mbi, 0, sizeof(mbi));
            if (::VirtualQuery(reinterpret_cast<void*>(ctx.Esp), &mbi,
                               sizeof(mbi))) {
                uint8_t* alloc_base =
                    static_cast<uint8_t*>(mbi.AllocationBase);
                uint8_t* p = alloc_base;
                while (true) {
                    MEMORY_BASIC_INFORMATION m;
                    memset(&m, 0, sizeof(m));
                    if (::VirtualQuery(p, &m, sizeof(m)) == 0) break;
                    if (static_cast<uint8_t*>(m.AllocationBase) != alloc_base)
                        break;
                    if (m.State == MEM_COMMIT) out.committed += m.RegionSize;
                    if (m.State == MEM_COMMIT || m.State == MEM_RESERVE)
                        out.reserved += m.RegionSize;
                    p = static_cast<uint8_t*>(m.BaseAddress) + m.RegionSize;
                }
            }
        }
        ::CloseHandle(ht);
    } while (::Thread32Next(snap, &te));

    ::CloseHandle(snap);
    return out;
}

}
