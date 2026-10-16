#pragma once

#include <cstddef>
#include <string>

namespace host_platform {

std::string ExecutablePath();

bool StdoutAvailable();
bool WriteStdout(const char* buf, size_t len);
bool WriteStderr(const char* buf, size_t len);

[[noreturn]] void ExitProcessNow(int code);

}
