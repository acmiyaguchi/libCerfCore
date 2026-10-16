#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace tps65950 {

inline double Linear(double db) { return std::pow(10.0, db / 20.0); }

inline int64_t ToQ16(double linear) { return std::llround(65536.0 * linear); }

inline int64_t GainQ16(double db) { return ToQ16(Linear(db)); }

inline int32_t Clamp16(int64_t v) { return static_cast<int32_t>(std::clamp<int64_t>(v, -32768, 32767)); }

inline int32_t ScaleQ16(int64_t sample, int64_t gain_q16) { return Clamp16((sample * gain_q16) >> 16); }

}
