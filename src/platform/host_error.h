#pragma once

#include <cstdint>
#include <string>

namespace host_platform {

uint32_t    LastErrorCode();
std::string ErrorText(uint32_t code);

}
