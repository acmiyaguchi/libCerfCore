#include "host_icmp_echo.h"

#include "host_unimplemented.h"

namespace host_platform {

IcmpEchoResult SendIcmpEcho(uint32_t, const uint8_t*, size_t, uint32_t) {
    NoHostImplementation("SendIcmpEcho");
}

}
