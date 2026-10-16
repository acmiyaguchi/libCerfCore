#pragma once

#include <cstdint>

namespace cerfcore {

class CerfEmulator;
class MediaQMq200Power;
class StateWriter;
class StateReader;

class MediaQMq200Memory {
public:
    static constexpr uint32_t kMm03R = 0x400Cu;

    MediaQMq200Memory(CerfEmulator& emu, const MediaQMq200Power& power)
        : emu_(emu), power_(power) {}

    void PowerOnReset();

    static bool Owns(uint32_t roff);
    uint32_t Read(uint32_t roff) const;
    void     Write(uint32_t roff, uint32_t value);

    const char* MiuBlocked() const;
    bool        MiuPoweredDown() const;

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    const char* MiuPowerDown() const;

    CerfEmulator&           emu_;
    const MediaQMq200Power& power_;

    uint32_t mm00_ = 0;
    uint32_t mm01_ = 0;
    uint32_t mm02_ = 0;
    uint32_t mm03_ = 0;
    uint32_t mm04_ = 0;
};

}
