#pragma once

#if defined(_MSC_VER)
#define CERF_FORCEINLINE __forceinline
#else
#define CERF_FORCEINLINE inline __attribute__((always_inline))
#endif
