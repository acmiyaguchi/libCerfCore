#include <cerfcore/platform/host_local_time.h>

namespace cerfcore {

namespace host_platform {

void LocalCalendarTime(std::time_t t, std::tm& out) { ::localtime_r(&t, &out); }

}

}
