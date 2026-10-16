#pragma once

#include <cstdarg>

namespace log_detail {

void RecordCautionLocked(const char* fmt, va_list args);

}
