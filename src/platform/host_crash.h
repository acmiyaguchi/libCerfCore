#pragma once

#include <cstdarg>
#include <cstddef>

namespace cerfcore {

namespace host_platform {

void CrashFreezeOtherThreads();
void CrashOpenLog();
void CrashWrite(const char* buf, size_t len);
int  CrashFormat(char* buf, size_t cap, const char* fmt, va_list ap);
void CrashDumpOtherThreads();
void CrashPrintNativeStack(const char* tag);
bool CrashSymbolize(const void* addr, char* out, size_t out_size);
void CrashInstallUnhandledFilter();

}

}
