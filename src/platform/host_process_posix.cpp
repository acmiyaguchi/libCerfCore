#include <cerfcore/platform/host_process.h>

#include "host_unimplemented.h"

namespace cerfcore {

namespace host_platform {

std::string ExecutablePath() { NoHostImplementation("ExecutablePath"); }
bool StdoutAvailable() { NoHostImplementation("StdoutAvailable"); }
bool WriteStdout(const char*, size_t) { NoHostImplementation("WriteStdout"); }
bool WriteStderr(const char*, size_t) { NoHostImplementation("WriteStderr"); }
void ExitProcessNow(int) { NoHostImplementation("ExitProcessNow"); }

}

}
