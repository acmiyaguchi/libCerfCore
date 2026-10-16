#pragma once

#include <cstdint>
#include <string>

namespace cerfcore {

namespace host_platform {

uint32_t    LastErrorCode();
std::string ErrorText(uint32_t code);

}

}
