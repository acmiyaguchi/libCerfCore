#include "host_icmp_echo.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>

namespace host_platform {

IcmpEchoResult SendIcmpEcho(uint32_t dest_ip_n, const uint8_t* payload, size_t size,
                            uint32_t timeout_ms) {
    IcmpEchoResult out;
    HANDLE h = IcmpCreateFile();
    if (h == INVALID_HANDLE_VALUE) return out;

    DWORD reply_size = (DWORD)(sizeof(ICMP_ECHO_REPLY) + size + 8);
    std::vector<uint8_t> reply_buf(reply_size);

    DWORD n = IcmpSendEcho(h, dest_ip_n,
                           size == 0 ? nullptr : const_cast<uint8_t*>(payload),
                           (WORD)size,
                           nullptr,
                           reply_buf.data(), reply_size,
                           timeout_ms);
    const DWORD send_error = (n == 0) ? GetLastError() : 0;
    IcmpCloseHandle(h);
    if (n == 0) {
        out.status = IcmpEchoStatus::kNoReply;
        out.code   = send_error;
        return out;
    }
    auto* er = reinterpret_cast<ICMP_ECHO_REPLY*>(reply_buf.data());
    if (er->Status != IP_SUCCESS) {
        out.status = IcmpEchoStatus::kFailed;
        out.code   = er->Status;
        return out;
    }
    out.status = IcmpEchoStatus::kReply;
    if (er->Data != nullptr) {
        const uint8_t* data = static_cast<const uint8_t*>(er->Data);
        out.data.assign(data, data + er->DataSize);
    } else {
        out.data.assign(er->DataSize, 0);
    }
    return out;
}

}
