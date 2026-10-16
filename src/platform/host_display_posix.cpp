#include "host_display.h"

#include "host_unimplemented.h"

namespace host_platform {

void LargestDisplaySize(uint32_t&, uint32_t&) { NoHostImplementation("LargestDisplaySize"); }
int  HighestDisplayRefreshRate() { NoHostImplementation("HighestDisplayRefreshRate"); }

}
