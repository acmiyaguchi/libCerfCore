#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

class WaveInFifo {
public:
    WaveInFifo();
    ~WaveInFifo();
    WaveInFifo(const WaveInFifo&)            = delete;
    WaveInFifo& operator=(const WaveInFifo&) = delete;

    void   Start(const char* log_tag, size_t cap_bytes);
    void   Stop();

    void   Request(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits);
    void   Clear();
    size_t Take(uint8_t* dst, size_t bytes);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
