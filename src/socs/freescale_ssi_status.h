#pragma once

#include "../execution/guest_cycle_clock.h"

#include <cstdint>
#include <functional>

class CerfEmulator;
class FreescaleSsiReceiver;
class FreescaleSsiTransmitter;
class StateReader;
class StateWriter;

struct FreescaleSsiStatusRules {
    bool frame_complete;
    bool write_one_clears;
    bool tfs_high_in_normal;
};

struct FreescaleSsiStatusControl {
    bool     enabled     = false;
    bool     te          = false;
    bool     re          = false;
    bool     network     = false;
    bool     two_channel = false;
    bool     tfen[2]     = {false, false};
    bool     rfen[2]     = {false, false};
    uint32_t sier        = 0;
};

class FreescaleSsiStatus {
public:
    FreescaleSsiStatus(CerfEmulator& emu, FreescaleSsiTransmitter& tx, FreescaleSsiReceiver& rx,
                       FreescaleSsiStatusRules rules)
        : emu_(emu), tx_(tx), rx_(rx), rules_(rules) {}

    void Attach(std::function<void(bool asserted)> line);
    void Reset();
    void SetControl(const FreescaleSsiStatusControl& c);

    uint32_t ReadSisr();
    void     WriteSisr(uint32_t value);
    void     StxWrite(uint32_t k);
    void     SrxRead(uint32_t k);
    void     Update();

    void Save(StateWriter& w);
    void Restore(StateReader& r);
    void PostRestore(const FreescaleSsiStatusControl& c);

private:
    uint32_t Forced() const;
    uint32_t Bits();
    uint32_t Sources() const;
    void     Drive(bool level);

    CerfEmulator&                 emu_;
    FreescaleSsiTransmitter&      tx_;
    FreescaleSsiReceiver&         rx_;
    const FreescaleSsiStatusRules rules_;
    GuestCycleClock*              clock_ = nullptr;
    GuestCycleClock::Event*       ev_    = nullptr;
    std::function<void(bool)>     line_;
    FreescaleSsiStatusControl     ctl_;
    bool                          asserted_    = false;
    bool                          tue_read_[2] = {false, false};
    bool                          roe_read_[2] = {false, false};
};
