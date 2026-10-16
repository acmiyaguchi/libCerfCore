#include "host_crash.h"

#include "host_unimplemented.h"

namespace host_platform {

void CrashFreezeOtherThreads() { NoHostImplementation("CrashFreezeOtherThreads"); }
void CrashOpenLog() { NoHostImplementation("CrashOpenLog"); }
void CrashWrite(const char*, size_t) { NoHostImplementation("CrashWrite"); }
int  CrashFormat(char*, size_t, const char*, va_list) { NoHostImplementation("CrashFormat"); }
void CrashDumpOtherThreads() { NoHostImplementation("CrashDumpOtherThreads"); }
void CrashPrintNativeStack(const char*) { NoHostImplementation("CrashPrintNativeStack"); }
bool CrashSymbolize(const void*, char*, size_t) { NoHostImplementation("CrashSymbolize"); }
void CrashInstallUnhandledFilter() { NoHostImplementation("CrashInstallUnhandledFilter"); }

}
