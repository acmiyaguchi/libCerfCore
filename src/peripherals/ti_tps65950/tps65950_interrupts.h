#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;
class Tps65950Board;

class Tps65950Interrupts : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    static bool OwnsPower(uint8_t sub) { return sub >= kPwrIsr1 && sub <= kPwrSihCtrl; }
    static bool OwnsPih(uint8_t sub) { return sub >= kPihIsrP1 && sub <= kPihSir; }

    uint8_t ReadPower(uint8_t sub);
    void    WritePower(uint8_t sub, uint8_t value);
    uint8_t ReadPih(uint8_t sub) const;
    void    WritePih(uint8_t sub, uint8_t value);

    void SetRtcIt(bool level);

    void Save(StateWriter& w);
    void Restore(StateReader& r);
    void PostRestore();

private:
    static constexpr uint8_t kPwrIsr1    = 0x2Eu;
    static constexpr uint8_t kPwrImr1    = 0x2Fu;
    static constexpr uint8_t kPwrIsr2    = 0x30u;
    static constexpr uint8_t kPwrImr2    = 0x31u;
    static constexpr uint8_t kPwrSir     = 0x32u;
    static constexpr uint8_t kPwrEdr1    = 0x33u;
    static constexpr uint8_t kPwrEdr2    = 0x34u;
    static constexpr uint8_t kPwrSihCtrl = 0x35u;
    static constexpr uint8_t kPihIsrP1   = 0x81u;
    static constexpr uint8_t kPihIsrP2   = 0x82u;
    static constexpr uint8_t kPihSir     = 0x83u;

    void LoadPowerOn();
    void Event(uint8_t bit);
    void Latch(int line, uint8_t bit);
    void Clear(int line, uint8_t bits);
    void Drive();

    Tps65950Board* board_      = nullptr;
    uint8_t        isr_[2]     = {0u, 0u};
    uint8_t        imr_[2]     = {0u, 0u};
    uint8_t        pending_[2] = {0u, 0u};
    uint8_t        edr_[2]     = {0u, 0u};
    uint8_t        sih_ctrl_   = 0u;
    bool           rtc_it_     = false;
    bool           int1_       = false;
    bool           reset_      = false;
};

}
