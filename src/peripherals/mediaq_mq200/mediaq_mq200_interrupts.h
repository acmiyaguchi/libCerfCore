#pragma once

#include <cstdint>

namespace cerfcore {

class CerfEmulator;
class StateWriter;
class StateReader;

class MediaQMq200Interrupts {
public:
    static constexpr uint32_t kIn00R = 0x8000u;
    static constexpr uint32_t kIn01R = 0x8004u;
    static constexpr uint32_t kIn02R = 0x8008u;

    explicit MediaQMq200Interrupts(CerfEmulator& emu) : emu_(emu) {}

    void PowerOnReset();

    static bool Owns(uint32_t roff);
    uint32_t Read(uint32_t roff) const;
    void     Write(uint32_t roff, uint32_t value);

    bool Gc1VdeFallingUnmasked() const { return (mask_ & kGc1VdeFalling) != 0u; }
    void OnGc1VdeFalling();

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    static constexpr uint32_t kGc1VdeFalling = 1u << 3;

    CerfEmulator& emu_;

    uint32_t control_ = 0;
    uint32_t mask_    = 0;
    uint32_t status_  = 0;
};

}
