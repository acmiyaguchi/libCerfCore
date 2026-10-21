#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace cerfcore {

namespace host_platform {

std::string ExecutablePath();
uint32_t    CurrentProcessId();

bool StdoutAvailable();
bool WriteStdout(const char* buf, size_t len);
bool WriteStderr(const char* buf, size_t len);

[[noreturn]] void ExitProcessNow(int code);

}

}
