#pragma once

#include <cstdarg>

namespace cerfcore {

namespace log_detail {

void RecordCautionLocked(const char* fmt, va_list args);

}

}
