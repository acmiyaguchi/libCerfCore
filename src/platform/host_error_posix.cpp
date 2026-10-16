#include "host_error.h"

#include "host_unimplemented.h"

namespace host_platform {

uint32_t    LastErrorCode() { NoHostImplementation("LastErrorCode"); }
std::string ErrorText(uint32_t) { NoHostImplementation("ErrorText"); }

}
