#include "host_event.h"

#include "host_unimplemented.h"

namespace cerfcore {

namespace host_platform {

HostEvent::~HostEvent() {
    if (handle_ != nullptr) NoHostImplementation("HostEvent::~HostEvent");
}

bool HostEvent::Create() { NoHostImplementation("HostEvent::Create"); }
void HostEvent::Set() { NoHostImplementation("HostEvent::Set"); }
void HostEvent::Wait(uint32_t) { NoHostImplementation("HostEvent::Wait"); }

}

}
