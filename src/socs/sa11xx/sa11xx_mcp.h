#pragma once

#include <cerfcore/peripherals/peripheral_base.h>
#include "../../execution/guest_cycle_clock.h"
#include <cerfcore/state/state_stream.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace cerfcore {

class Sa11xxDma;
class Sa11xxIntc;
class Sa11xxMcpAudioStream;
class Sa11xxMcpFrameClock;
class Sa11xxMcpTelecomStream;

class Sa11xxMcp : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x80060000u; }
    uint32_t MmioSize() const override { return 0x00000060u; }

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    uint32_t Mccr0() const { return mccr0_; }
    void     RegisterControlListener(std::function<void()> fn);

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

private:
    static constexpr uint64_t kNever = UINT64_MAX;

    void     RouteCodecCommand(uint32_t cmd);
    void     ControlChanged();
    void     ResumeFrames(uint64_t now);
    void     SettleChannels(uint64_t now);
    void     RefreshLine(uint64_t now);
    void     ServeCommand(uint64_t now);
    void     PerformWrite(uint64_t now, uint64_t carry, uint8_t reg, uint16_t value);
    uint16_t ReturnedRead(uint8_t reg, uint16_t sent);
    bool     Looped() const;
    void     ClearCompletion();

    std::vector<std::function<void()>> control_listeners_;
    Sa11xxMcpAudioStream*   audio_   = nullptr;
    Sa11xxMcpTelecomStream* telecom_ = nullptr;
    Sa11xxMcpFrameClock*    frames_  = nullptr;
    Sa11xxIntc*             intc_    = nullptr;
    GuestCycleClock*        clock_   = nullptr;
    GuestCycleClock::Event* line_ev_ = nullptr;
    Sa11xxDma*              dma_     = nullptr;
    uint32_t mccr0_      = 0;
    bool     cmd_valid_  = false;
    bool     cmd_write_  = false;
    bool     cmd_done_   = false;
    uint8_t  cmd_reg_    = 0;
    uint16_t cmd_value_  = 0;
    uint16_t cmd_sent_   = 0;
    bool     cwc_        = false;
    bool     crc_        = false;
    uint32_t mcdr2_      = 0;
};

}
