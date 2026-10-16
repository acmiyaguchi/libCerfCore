#pragma once

#include <cstdint>

class CerfEmulator;
class StateWriter;
class StateReader;

class MediaQMq200Fp {
public:
    static constexpr uint32_t kNoPanel = 0xFFFFFFFFu;

    explicit MediaQMq200Fp(CerfEmulator& emu) : emu_(emu) {}

    void PowerOnReset();

    static bool Owns(uint32_t roff);
    uint32_t Read(uint32_t roff) const;
    void     Write(uint32_t roff, uint32_t value);

    uint32_t PanelGc() const;
    bool     PanelPinsHigh() const;

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    static constexpr uint32_t kRegs = 0x38u;

    CerfEmulator& emu_;

    uint32_t regs_[kRegs] = {};
};
