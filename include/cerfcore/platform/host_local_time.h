#pragma once

#include <ctime>

namespace cerfcore {

namespace host_platform {

void LocalCalendarTime(std::time_t t, std::tm& out);

}

}
