#pragma once

#include <slirp/libslirp.h>

#include "../platform/host_network.h"

struct PollFillCtx {
    host_platform::HostPollSet* fds;
};

int cb_add_poll_socket(slirp_os_socket fd, int events, void* opaque);
int cb_get_revents(int idx, void* opaque);
