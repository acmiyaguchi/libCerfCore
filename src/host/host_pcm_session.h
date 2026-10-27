#pragma once

#include "paced_wave_out.h"

#include <chrono>
#include <cstdint>

namespace cerfcore {

class CerfEmulator;

class HostPcmSession {
public:
    HostPcmSession(CerfEmulator& emu, const char* log_tag, uint16_t channels, uint16_t bits)
        : emu_(emu), log_tag_(log_tag), channels_(channels), bits_(bits) {}

    void Open();
    void Close();
    void Queue(const void* bytes, uint32_t length, uint32_t rate_hz);
    void Finish();
    void Abort();

private:
    CerfEmulator& emu_;
    const char*   log_tag_;
    uint16_t      channels_;
    uint16_t      bits_;
    PacedWaveOut  host_;
    bool          active_ = false;
    uint32_t      rate_   = 0;
    uint64_t      stream_bytes_ = 0;
    uint32_t      stream_peak_  = 0;
    std::chrono::steady_clock::time_point last_report_;

    void ReportStream();
};

}
