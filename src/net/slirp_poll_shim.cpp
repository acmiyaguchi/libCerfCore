#include "slirp_poll_shim.h"

namespace {

int SlirpEventsToHost(int slirp_events) {
    int out = 0;
    if (slirp_events & SLIRP_POLL_IN)  out |= host_platform::kPollReadable;
    if (slirp_events & SLIRP_POLL_OUT) out |= host_platform::kPollWritable;
    if (slirp_events & SLIRP_POLL_PRI) out |= host_platform::kPollPriority;
    return out;
}

int HostEventsToSlirp(int host_revents) {
    int out = 0;
    if (host_revents & host_platform::kPollReadable) out |= SLIRP_POLL_IN;
    if (host_revents & host_platform::kPollWritable) out |= SLIRP_POLL_OUT;
    if (host_revents & host_platform::kPollPriority) out |= SLIRP_POLL_PRI;
    if (host_revents & host_platform::kPollError)    out |= SLIRP_POLL_ERR;
    if (host_revents & host_platform::kPollHangUp)   out |= SLIRP_POLL_HUP;
    return out;
}

}

int cb_add_poll_socket(slirp_os_socket fd, int events, void* opaque) {
    auto* ctx = static_cast<PollFillCtx*>(opaque);
    return ctx->fds->Add(static_cast<uintptr_t>(fd), SlirpEventsToHost(events));
}

int cb_get_revents(int idx, void* opaque) {
    auto* ctx = static_cast<PollFillCtx*>(opaque);
    return HostEventsToSlirp(ctx->fds->Revents(idx));
}
