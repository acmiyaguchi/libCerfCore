#pragma once

#include <cstdint>

class CerfEmulator;
class MediaQMq200Ge;
class StateWriter;
class StateReader;

class MediaQMq200GeWindow {
public:
    static constexpr uint32_t kCommandRegs = 12u;

    MediaQMq200GeWindow(CerfEmulator& emu, MediaQMq200Ge& ge) : emu_(emu), ge_(ge) {}

    void PowerOnReset();

    static bool Owns(uint32_t roff);
    uint32_t Read(uint32_t roff, const char* blocked);
    void     Write(uint32_t roff, uint32_t value, const char* blocked);

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    CerfEmulator&  emu_;
    MediaQMq200Ge& ge_;
    uint32_t       secondary_[kCommandRegs] = {};
};
