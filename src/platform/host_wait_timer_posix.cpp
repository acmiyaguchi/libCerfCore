#include "host_wait_timer.h"

#include "host_unimplemented.h"

namespace host_platform {

HostWaitTimer::~HostWaitTimer() {
    if (handle_ != nullptr) NoHostImplementation("HostWaitTimer::~HostWaitTimer");
}

bool HostWaitTimer::Create() { NoHostImplementation("HostWaitTimer::Create"); }
void HostWaitTimer::Close() {
    if (handle_ != nullptr) NoHostImplementation("HostWaitTimer::Close");
}
bool HostWaitTimer::Arm(int64_t) { NoHostImplementation("HostWaitTimer::Arm"); }
void HostWaitTimer::Wait() { NoHostImplementation("HostWaitTimer::Wait"); }
bool HostWaitTimer::WaitOrEvent(void*) { NoHostImplementation("HostWaitTimer::WaitOrEvent"); }
void HostWaitTimer::Cancel() { NoHostImplementation("HostWaitTimer::Cancel"); }

}
