#pragma once

#include <cerfcore/state/state_stream.h>
#include "../../execution/guest_cycle_clock.h"
#include "../oscillator_ticks.h"
#include "imx28_apb_dma_device.h"
#include "imx28_apb_peripheral.h"
#include "imx28_saif_regs.h"
#include "imx28_sct_reg.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

class Imx28Apbx;
class Imx28Digctl;
class Imx28SaifHostInput;
class Imx28SaifHostOutput;

/* MCIMX28RM Rev. 2 chapter 35 (SAIF), one instance per unit. */
class Imx28Saif : public Imx28ApbPeripheral, private Imx28ApbDmaDevice {
public:
    Imx28Saif(CerfEmulator& emu, uint32_t unit, uint32_t base)
        : Imx28ApbPeripheral(emu), unit_(unit), base_(base) {}

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return base_; }
    uint32_t MmioSize() const override { return 0x00002000u; }

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;
    void     SaveState(StateWriter& w) override;
    void     RestoreState(StateReader& r) override;
    void     PostRestore() override;

    bool                  DrivesLrclk() const;
    GuestCycleClock::Rate FrameRate() const;

private:
    void     ResetBlock();
    void     WriteCtrl(Imx28SctOp op, uint32_t value, uint32_t addr);
    void     ApplyCtrl(Imx28SctOp op, uint32_t value, uint32_t addr);
    void     WriteStat(Imx28SctOp op, uint32_t value, uint32_t addr);
    uint32_t ComposeStat() const;
    void     StartBlock();
    void     GateClock(bool was, bool now);
    void     OnBlockEnd();
    void     Rescale();
    [[noreturn]] void Die(const char* what, uint32_t addr, uint32_t value) const;

    Imx28Saif* ClockSource() const;
    bool       Receiving() const;
    void       CheckReceive(uint32_t next, uint32_t addr, uint32_t value) const;
    void       RxSync();
    void       RxUpdate();
    void       RxArm();
    void       RxPush(uint64_t frames);
    void       OnRxEvent();
    void       ResetRx();
    uint32_t   ReadData(uint32_t addr);
    Transfer   RxDma(uint8_t* dst, uint32_t count);
    void       SaveRx(StateWriter& w);
    void       RestoreRx(StateReader& r);

    void     DmaPioWrite(uint32_t channel, uint32_t index, uint32_t value) override;
    Transfer DmaToMemory(uint32_t channel, uint8_t* dst, uint32_t count) override;
    Transfer DmaFromMemory(uint32_t channel, const uint8_t* src, uint32_t count) override;
    bool     DmaCommandEnded(uint32_t channel) override;
    void     DmaChannelReset(uint32_t channel) override;

    uint32_t unit_;
    uint32_t base_;

    Imx28Apbx*             apbx_   = nullptr;
    Imx28SaifHostOutput*   host_   = nullptr;
    Imx28SaifHostInput*    mic_    = nullptr;
    Imx28Digctl*           digctl_ = nullptr;
    Imx28Saif*             peer_   = nullptr;
    GuestCycleClock*       clock_  = nullptr;
    GuestCycleClock::Event* event_ = nullptr;
    GuestCycleClock::Event* rx_event_ = nullptr;
    OscillatorTicks        frames_{emu_, false};

    uint32_t             ctrl_    = 0u;
    uint32_t             stat_    = 0u;
    std::vector<uint8_t> block_;
    uint8_t              playing_ = 0u;
    uint64_t             end_     = 0u;
    uint64_t             remaining_ = 0u;

    uint8_t              rx_live_  = 0u;
    uint64_t             rx_mark_  = 0u;
    uint32_t             rx_fifo_[imx28_saif::kRxFifoEntries] = {};
    uint32_t             rx_count_ = 0u;
    uint32_t             rx_want_  = 0u;
    std::vector<uint8_t> rx_dma_;
};

class Imx28Saif0 : public Imx28Saif {
public:
    explicit Imx28Saif0(CerfEmulator& emu) : Imx28Saif(emu, 0u, 0x80042000u) {}
};

class Imx28Saif1 : public Imx28Saif {
public:
    explicit Imx28Saif1(CerfEmulator& emu) : Imx28Saif(emu, 1u, 0x80046000u) {}
};

}
