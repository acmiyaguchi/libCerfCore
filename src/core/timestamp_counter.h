#pragma once

#include <cstdint>

#if defined(_M_IX86) || defined(_M_X64)
#include <intrin.h>
#elif defined(__i386__) || defined(__x86_64__)
#include <x86intrin.h>
#else
#include <chrono>
#endif

inline uint64_t ReadTimestampCounter() {
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
    return __rdtsc();
#else
    return static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}
