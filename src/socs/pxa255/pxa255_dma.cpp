#include "../pxa2xx/pxa2xx_dma_moves.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "pxa255_id.h"

#include <cstdint>

namespace {

class Pxa255Dma : public Pxa2xxDmaMoves {
public:
    using Pxa2xxDmaMoves::Pxa2xxDmaMoves;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Pxa255;
    }

    /* Intel PXA255 Developer's Manual section 3.4.2 (page 3-7): "In Watchdog Reset all units in the
       are reset except the Clocks and Power Manager"; section 3.4.3 (page 3-8): in GPIO Reset "all
       processor units except the RTC, parts of the Clocks and Power Manager, and the Memory Controller". */
    void OnReady() override {
        AttachChannels(kNumChannels);
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { ResetLine(); });
    }

    uint32_t MmioBase() const override { return 0x40000000u; }
    uint32_t MmioSize() const override { return 0x00001000u; }

    uint8_t ReadByte(uint32_t addr) override {
        Locked lk(*this);
        return static_cast<uint8_t>(ReadRegLocked(addr & ~0x3u) >> ((addr & 0x3u) * 8u));
    }
    uint32_t ReadWord(uint32_t addr) override {
        Locked lk(*this);
        return ReadRegLocked(addr);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        Locked lk(*this);
        const uint32_t base = addr & ~0x3u, shift = (addr & 0x3u) * 8u;
        WriteRegLocked(base, (ReadRegLocked(base) & ~(0xFFu << shift))
                             | (static_cast<uint32_t>(value) << shift));
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        Locked lk(*this);
        WriteRegLocked(addr, value);
    }

    void SaveState(StateWriter& w) override {
        Locked lk(*this);
        SaveChannels(w);
        w.WriteBytes("drcmr", drcmr_, sizeof(drcmr_));
    }

    void RestoreState(StateReader& r) override {
        Locked lk(*this);
        RestoreChannels(r);
        r.ReadBytes("drcmr", drcmr_, sizeof(drcmr_));
    }

    void PostRestore() override {
        Locked lk(*this);
        PostRestoreChannelsLocked();
    }

protected:
    uint32_t DrcmrOf(uint32_t request) const override {
        return request < kNumDrcmr ? (drcmr_[request] & kDrcmrDefined) : 0u;
    }
    uint32_t DescriptorDdadrMask() const override { return 0xFFFFFFFFu; }
    uint32_t DescriptorDcmdMask() const override { return kDcmdDefined; }

    /* Intel PXA255 Developer's Manual Table 5-7 (page 5-19) STOPSTATE: "If the channel is in the
       uninitialized or stopped state, this status bit is set. If the DCSR[STOPIRQEN] is set to 1,
       the DMAC generates an interrupt." */
    bool ChannelIrq(uint32_t ch) const override {
        const uint32_t d = dcsr_[ch];
        if ((d & (BUSERRINTR | STARTINTR | ENDINTR)) != 0u) return true;
        return ChannelStoppedLocked(ch) && (d & STOPIRQEN) != 0u;
    }

    bool DiscardsEmptyDescriptor() const override { return false; }

    /* Intel PXA255 Developer's Manual section 5.3.5 (page 5-21): "DSADR cannot contain the address of any
       other internal DMA, LCD, or MEMC registers."; section 5.2 (page 5-11): "Writes to flash are not
       supported and cause a bus error." */
    bool MoveBusErrorLocked(uint32_t pa, bool write) override {
        if (pa - MmioBase() < MmioSize()) {
            emu_.Get<Fatal>().Die("Pxa255Dma: DMA %s DMA controller register 0x%08X; not modelled",
                                  write ? "write to" : "read of", pa);
        }
        auto& mem = emu_.Get<EmulatedMemory>();
        return write && mem.TryTranslate(pa) != nullptr && mem.TryTranslateWrite(pa) == nullptr;
    }

    bool DescriptorInMemoryOnly() const override { return false; }

    /* Intel PXA255 Developer's Manual Table 5-7 (page 5-18): DCSR bits 7:4 reserved. */
    void RequestAfterStopLocked(uint64_t, uint64_t&) override {}

private:
    /* Intel PXA255 Developer's Manual Table 5-7 (page 5-18): STOPSTATE bit 3. */
    static constexpr uint32_t STOPSTATE = 1u << 3;
    /* Intel PXA255 Developer's Manual Table 5-12 (page 5-24): bits 27:23, 20:19 and 13 reserved. */
    static constexpr uint32_t kDcmdDefined = 0xF067DFFFu;
    /* Intel PXA255 Developer's Manual Table 5-8 (page 5-20): MAPVLD bit 7, CHLNUM 3:0. */
    static constexpr uint32_t kDrcmrDefined = 0x8Fu;
    static constexpr uint32_t kNumChannels = 16, kNumDrcmr = 40;

    uint32_t drcmr_[kNumDrcmr] = {};

    void ResetLine() {
        Locked lk(*this);
        ResetChannelsLocked();
        for (uint32_t& d : drcmr_) d = 0u;
    }

    uint32_t ReadRegLocked(uint32_t addr) {
        const uint32_t off = addr - MmioBase();
        if (off < 0x40u) {
            const uint32_t ch = off / 4u;
            uint32_t       v  = ChannelStoppedLocked(ch) ? (dcsr_[ch] | STOPSTATE) : dcsr_[ch];
            if (RequestPendingLocked(ch)) v |= REQPEND;
            return v;
        }
        if (off == 0xF0u) return Dint();
        if (off >= 0x100u && off < 0x1A0u) return drcmr_[(off - 0x100u) / 4u];
        if (off >= 0x200u && off < 0x300u) {
            const uint32_t ch = (off - 0x200u) / 0x10u;
            switch (off & 0xFu) {
                case 0x0u: return ChannelRegLocked(ch, Reg::Ddadr);
                case 0x4u: return ChannelRegLocked(ch, Reg::Dsadr);
                case 0x8u: return ChannelRegLocked(ch, Reg::Dtadr);
                case 0xCu: return ChannelRegLocked(ch, Reg::Dcmd);
            }
        }
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }

    void WriteRegLocked(uint32_t addr, uint32_t value) {
        const uint32_t off = addr - MmioBase();
        if (off < 0x40u) { WriteDcsrLocked(off / 4u, value); return; }
        if (off == 0xF0u) return;
        if (off >= 0x100u && off < 0x1A0u) {
            const uint32_t idx = (off - 0x100u) / 4u;
            drcmr_[idx] = value;
            LOG(SocDma, "DRCMR%u <= 0x%08X (mapvld=%u ch=%u)\n", idx, value,
                (value >> 7) & 1u, value & 0x1Fu);
            DrcmrWrittenLocked(idx);
            return;
        }
        if (off >= 0x200u && off < 0x300u) {
            const uint32_t ch = (off - 0x200u) / 0x10u;
            switch (off & 0xFu) {
                case 0x0u: ddadr_[ch] = value; return;
                case 0x4u: if (!DescriptorFetchModeLocked(ch)) WriteTransferLocked(dsadr_, ch, value); return;
                case 0x8u: if (!DescriptorFetchModeLocked(ch)) WriteTransferLocked(dtadr_, ch, value); return;
                case 0xCu: if (!DescriptorFetchModeLocked(ch)) WriteTransferLocked(dcmd_, ch, value); return;
            }
        }
        HaltUnsupportedAccess("WriteWord", addr, value);
    }

    void WriteDcsrLocked(uint32_t ch, uint32_t value) {
        uint32_t cur = dcsr_[ch] & ~(value & (BUSERRINTR | STARTINTR | ENDINTR));
        const uint32_t rw = RUN | NODESCFETCH | STOPIRQEN;
        const bool was_run = (dcsr_[ch] & RUN) != 0u;
        cur = (cur & ~rw) | (value & rw);
        dcsr_[ch] = cur;
        RunEdgeLocked(ch, was_run);
        UpdateIrqLocked();
    }

    uint32_t Dint() const {
        uint32_t d = 0;
        for (uint32_t ch = 0; ch < kNumChannels; ++ch)
            if (ChannelIrq(ch)) d |= (1u << ch);
        return d;
    }
};

}

REGISTER_SERVICE_AS(Pxa255Dma, Pxa2xxDma);
