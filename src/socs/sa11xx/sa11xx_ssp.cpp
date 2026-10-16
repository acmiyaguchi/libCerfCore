#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "sa1110_id.h"
#include "sa1100_id.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "sa11xx_dma.h"
#include "sa11xx_external_clocks.h"
#include "sa11xx_ssp_regs.h"
#include "sa11xx_ssp_stream.h"

namespace {

class Sa11xxSsp : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && (bd->GetSocId() == SocId::Sa1110 || bd->GetSocId() == SocId::Sa1100);
    }
    void OnReady() override {
        clock_  = &emu_.Get<GuestCycleClock>();
        dma_    = &emu_.Get<Sa11xxDma>();
        stream_ = &emu_.Get<Sa11xxSspStream>();
        /* SA-1110 Developer's Manual §11.12.9.3 (printed 11-157): "SSE is the only control bit
           within the SSP that is reset to a known state". */
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            sscr0_ &= ~sa11xx_ssp::kSse;
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x80070000u; }
    uint32_t MmioSize() const override { return 0x00010000u; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override {
        emu_.Get<Sa11xxExternalClocks>().Resync();
        stream_->PostRestore(clock_->Cycles());
    }

private:
    Sa11xxSspStream* stream_ = nullptr;
    GuestCycleClock* clock_  = nullptr;
    Sa11xxDma*       dma_    = nullptr;
    uint32_t sscr0_   = 0;
    uint32_t sscr1_   = 0;

    void ControlChanged() {
        stream_->WriteControl(clock_->Cycles(), sscr0_, sscr1_);
        dma_->OnPortChange();
    }

    uint32_t ReadReg(uint32_t off) {
        switch (off) {
            case 0x60: return sscr0_;
            case 0x64: return sscr1_;
            case 0x6C: return stream_->ReadData(clock_->Cycles());
            case 0x74: return stream_->Status(clock_->Cycles());
            default:   return 0;
        }
    }
    void WriteReg(uint32_t off, uint32_t value) {
#if CERF_DEV_MODE
        const char* nm =
            off == 0x60 ? "SSCR0" :
            off == 0x64 ? "SSCR1" :
            off == 0x6C ? "SSDR " :
            off == 0x74 ? "SSSR " : "?    ";
        LOG(Periph, "[Sa11xxSsp] W %s (+0x%02X) = 0x%08X\n", nm, off, value);
#endif
        switch (off) {
            case 0x60: sscr0_ = value; ControlChanged(); break;
            case 0x64: sscr1_ = value; ControlChanged(); break;
            case 0x6C:
                stream_->WriteData(clock_->Cycles(), static_cast<uint16_t>(value));
                dma_->OnPortChange();
                break;
            case 0x74:
                if ((value & sa11xx_ssp::kRor) != 0u) stream_->ClearOverrun(clock_->Cycles());
                break;
            default:   break;
        }
    }
    static bool IsKnown(uint32_t off) {
        return off == 0x60 || off == 0x64 || off == 0x6C || off == 0x74;
    }
};

uint8_t Sa11xxSsp::ReadByte(uint32_t addr) {
    const uint32_t off   = addr - MmioBase();
    const uint32_t base  = off & ~0x3u;
    const uint32_t shift = (off & 0x3u) * 8;
    if (!IsKnown(base)) HaltUnsupportedAccess("ReadByte", addr, 0);
    return static_cast<uint8_t>((ReadReg(base) >> shift) & 0xFFu);
}

uint32_t Sa11xxSsp::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (!IsKnown(off)) HaltUnsupportedAccess("ReadWord", addr, 0);
    return ReadReg(off);
}

void Sa11xxSsp::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off   = addr - MmioBase();
    const uint32_t base  = off & ~0x3u;
    const uint32_t shift = (off & 0x3u) * 8;
    if (!IsKnown(base)) HaltUnsupportedAccess("WriteByte", addr, value);
    if (base == 0x6C) {
        WriteReg(base, static_cast<uint32_t>(value) << shift);
        return;
    }
    if (base == 0x74) {
        if (shift == 0u) WriteReg(base, value);
        return;
    }
    const uint32_t cur     = ReadReg(base);
    const uint32_t cleared = cur & ~(0xFFu << shift);
    WriteReg(base, cleared | (static_cast<uint32_t>(value) << shift));
}

void Sa11xxSsp::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (!IsKnown(off)) HaltUnsupportedAccess("WriteWord", addr, value);
    WriteReg(off, value);
}

void Sa11xxSsp::SaveState(StateWriter& w) {
    w.Write("sscr0", sscr0_);  w.Write("sscr1", sscr1_);
    stream_->Save(w);
}

void Sa11xxSsp::RestoreState(StateReader& r) {
    r.Read("sscr0", sscr0_);  r.Read("sscr1", sscr1_);
    stream_->Restore(r);
}

}

REGISTER_SERVICE(Sa11xxSsp);
