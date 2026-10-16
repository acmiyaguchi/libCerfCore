#pragma once

#if defined(_MSC_VER)
#define CERF_UNREACHABLE() __assume(0)
#else
#define CERF_UNREACHABLE() __builtin_unreachable()
#endif
