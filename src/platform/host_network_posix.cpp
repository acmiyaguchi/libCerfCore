#include "host_network.h"

#include "host_unimplemented.h"

namespace host_platform {

bool StartNetworking(int&) { NoHostImplementation("StartNetworking"); }

TcpConnectProbe ProbeTcpConnect6(const uint8_t (&)[16], uint16_t, int) {
    NoHostImplementation("ProbeTcpConnect6");
}

struct HostPollSet::Entries {};

HostPollSet::HostPollSet() { NoHostImplementation("HostPollSet::HostPollSet"); }
HostPollSet::~HostPollSet() = default;

void HostPollSet::Clear() { NoHostImplementation("HostPollSet::Clear"); }
bool HostPollSet::Empty() const { NoHostImplementation("HostPollSet::Empty"); }
int  HostPollSet::Add(uintptr_t, int) { NoHostImplementation("HostPollSet::Add"); }
int  HostPollSet::Revents(int) const { NoHostImplementation("HostPollSet::Revents"); }
int  HostPollSet::Wait(int) { NoHostImplementation("HostPollSet::Wait"); }

}
