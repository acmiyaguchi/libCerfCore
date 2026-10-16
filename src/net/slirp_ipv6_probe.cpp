#include <cstdint>

#include <cerfcore/core/log.h>
#include "../platform/host_network.h"

bool ProbeHostIpv6Reachable() {
    LOG(Net, "IPv6 probe: testing public v6 reachability via TCP connect "
             "to 2606:4700:4700::1111:53 (Cloudflare DNS anycast, 1.5s timeout); "
             "result controls whether libslirp advertises itself as the guest's "
             "IPv6 default router and whether AAAA DNS records are passed through "
             "to the guest...\n");
    static const uint8_t cloudflare_v6[16] = {
        0x26, 0x06, 0x47, 0x00, 0x47, 0x00, 0, 0,
        0, 0, 0, 0, 0, 0, 0x11, 0x11
    };
    const host_platform::TcpConnectProbe probe =
        host_platform::ProbeTcpConnect6(cloudflare_v6, 53, 1500);
    if (!probe.socket_created) {
        LOG(Net, "IPv6 probe: socket(AF_INET6) failed (err=%d) -> no v6\n", probe.error);
        return false;
    }
    LOG(Net, "IPv6 probe -> 2606:4700:4700::1111:53 = %s (err=%d)\n",
        probe.reachable ? "REACHABLE" : "UNREACHABLE", probe.error);
    return probe.reachable;
}
