#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../execution/guest_cycle_clock.h"
#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include "pxa255_i2s_bit_clock.h"
#include "pxa255_i2s_fifo_reset_sync.h"
#include "pxa255_i2s_fifo_values.h"
#include "pxa255_i2s_interrupt.h"
#include "pxa255_i2s_stream.h"
#include "pxa255_id.h"

#include <cstdint>

namespace {

/* Intel PXA255 Developer's Manual Table 14-3 (page 14-9) SACR0: ENB 0, BCKD 2, RST 3, EFWR 4, STRF 5,
   TFTH 11:8, RFTH 15:12. */
constexpr uint32_t kSacr0Enb     = 1u << 0;
constexpr uint32_t kSacr0Rst     = 1u << 3;
constexpr uint32_t kSacr0Defined = 0xFF3Du;

class Pxa255I2s : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Pxa255;
    }

    void OnReady() override {
        clock_     = &emu_.Get<GuestCycleClock>();
        stream_    = &emu_.Get<Pxa255I2sStream>();
        irq_       = &emu_.Get<Pxa255I2sInterrupt>();
        bit_clock_ = &emu_.Get<Pxa255I2sBitClock>();
        values_    = &emu_.Get<Pxa255I2sFifoValues>();
        resets_    = &emu_.Get<Pxa255I2sFifoResetSync>();
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            stream_->ResetLine();
            irq_->ResetRegisters();
        });
    }

    uint32_t MmioBase() const override { return 0x40400000u; }
    uint32_t MmioSize() const override { return 0x00001000u; }

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override {
        stream_->Save(w);
        resets_->Save(w);
        bit_clock_->Save(w);
        values_->Save(w);
        irq_->Save(w);
    }
    void RestoreState(StateReader& r) override {
        stream_->Restore(r);
        resets_->Restore(r);
        bit_clock_->Restore(r);
        values_->Restore(r);
        irq_->Restore(r);
    }
    void PostRestore() override {
        bit_clock_->PostRestore();
        irq_->PostRestore();
    }

private:
    enum : uint32_t {
        kSACR0 = 0x00u, kSACR1 = 0x04u, kSASR0 = 0x0Cu, kSAIMR = 0x14u, kSAICR = 0x18u,
        kSADIV = 0x60u, kSADR = 0x80u,
    };

    void WriteSacr0(uint64_t now, uint32_t value);

    uint32_t ReadSasr0(uint64_t now);

    GuestCycleClock*     clock_     = nullptr;
    Pxa255I2sStream*     stream_    = nullptr;
    Pxa255I2sInterrupt*  irq_       = nullptr;
    Pxa255I2sBitClock*   bit_clock_ = nullptr;
    Pxa255I2sFifoValues* values_    = nullptr;
    Pxa255I2sFifoResetSync* resets_ = nullptr;
};

uint32_t Pxa255I2s::ReadWord(uint32_t addr) {
    const uint64_t now = clock_->Cycles();
    switch (addr - MmioBase()) {
    case kSACR0: return stream_->Sacr0();
    case kSACR1: return stream_->Sacr1();
    case kSASR0: return ReadSasr0(now);
    case kSAIMR: return irq_->Saimr();
    case kSAICR: return 0u;
    case kSADIV: return bit_clock_->Sadiv();
    case kSADR:  return stream_->ReadData(now);
    }
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

/* Intel PXA255 Developer's Manual Table 14-3 (page 14-9) RST: "Resets FIFOs logic and all registers, except
   this register (SACR0)", "1 = Reset is active to other registers". */
void Pxa255I2s::WriteWord(uint32_t addr, uint32_t value) {
    const uint64_t now  = clock_->Cycles();
    const uint32_t off  = addr - MmioBase();
    const bool     held = stream_->InReset();
    switch (off) {
    case kSACR0: WriteSacr0(now, value); return;
    case kSADR:  stream_->WriteData(now, value); return;
    case kSASR0: return;
    case kSACR1:
    case kSADIV:
    case kSAIMR:
    case kSAICR:
        if (held) return;
        if (off == kSACR1) stream_->WriteSacr1(now, value);
        if (off == kSADIV) bit_clock_->WriteSadiv(now, value);
        if (off == kSAIMR) irq_->WriteSaimr(value);
        if (off == kSAICR) irq_->WriteSaicr(now, value);
        return;
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

uint32_t Pxa255I2s::ReadSasr0(uint64_t now) {
    const uint32_t live = stream_->Sasr0(now);
    return live | irq_->Sticky();
}

void Pxa255I2s::WriteSacr0(uint64_t now, uint32_t value) {
    const uint32_t v          = value & kSacr0Defined;
    const bool     reset_rise = (v & kSacr0Rst) != 0u && !stream_->InReset();
    if (stream_->Enabled() && (v & kSacr0Enb) == 0u) {
        stream_->Disable(now, v);
    } else if (stream_->Enabled() || (v & kSacr0Enb) == 0u) {
        stream_->ChangeSacr0(now, v);
    } else {
        stream_->Enable(now, v);
    }
    if (reset_rise) stream_->ResetHeld(now);
}

}  // namespace

REGISTER_SERVICE(Pxa255I2s);
