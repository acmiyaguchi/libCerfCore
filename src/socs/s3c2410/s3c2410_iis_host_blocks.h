#pragma once

#include <array>
#include <cstdint>

namespace cerfcore {

class CerfEmulator;

class S3C2410IisHostBlocks {
public:
    static constexpr uint32_t kBlockBytes = 2048u;

    struct Completed {
        uint8_t  block[2][kBlockBytes];
        uint32_t count = 0;
    };

    explicit S3C2410IisHostBlocks(CerfEmulator& emu) : emu_(emu) {}

    void     Reset() { fill_ = 0u; }
    uint32_t Fill() const { return fill_; }
    void     Push(uint16_t sample, Completed& out);
    void     Pad(Completed& out);

private:
    void Complete(Completed& out);

    CerfEmulator&                    emu_;
    std::array<uint8_t, kBlockBytes> block_{};
    uint32_t                         fill_ = 0u;
};

}
