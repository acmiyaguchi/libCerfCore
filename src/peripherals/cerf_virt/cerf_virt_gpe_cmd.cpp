#include <cerfcore/peripherals/cerf_virt/cerf_virt_gpe_cmd.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_blt_descriptor.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_grad_descriptor.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_line_descriptor.h>
#include "cerf_virt_blitter.h"
#include "cerf_virt_gradient_filler.h"
#include "cerf_virt_line_drawer.h"
#include <cerfcore/peripherals/cerf_virt/cerf_virt_addr_map.h>
#include "cerf_virt_dma_arena.h"

#include <cerfcore/peripherals/peripheral_base.h>
#include "../peripheral_dispatcher.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/log.h>

#include <cstring>

namespace cerfcore {

namespace {

class CerfVirtGpeCmd : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<DeviceConfig>().guest_additions;
    }

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        std::memset(regs_, 0, sizeof(regs_));
        regs_[CerfVirt::kGpeCmdStatus / 4] = CerfVirt::kGpeStatusIdle;
    }

    uint32_t MmioBase() const override {
        return emu_.Get<BoardContext>().GuestAdditionsWindowBase() + CerfVirt::kGpeCmdOffset;
    }
    uint32_t MmioSize() const override { return CerfVirt::kGpeCmdSize; }

    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off >= sizeof(regs_)) return 0u;
        return regs_[off / 4];
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off == CerfVirt::kGpeCmdKick)     { ExecuteCmd();     return; }
        if (off == CerfVirt::kGpeCmdGradKick) { ExecuteGradCmd(); return; }
        if (off == CerfVirt::kGpeCmdLineKick) { ExecuteLineCmd(); return; }
        if (off >= sizeof(regs_)) return;
        if (off == CerfVirt::kGpeCmdStatus) return;
        regs_[off / 4] = value;
    }

private:
    void ExecuteCmd() {
        regs_[CerfVirt::kGpeCmdStatus / 4] = CerfVirt::kGpeStatusBusy;
        const uint32_t off = regs_[CerfVirt::kGpeCmdDescVa / 4];

        uint8_t* p = emu_.Get<CerfVirtDmaArena>().At(
            off, (uint32_t)sizeof(CerfVirt::CerfBltDescriptor));
        if (!p) {
            LOG(Cerf, "[CerfVirtGpeCmd] blit descriptor offset 0x%X outside arena\n",
                off);
            CerfFatalExit();
        }
        const CerfVirt::CerfBltDescriptor& d =
            *reinterpret_cast<const CerfVirt::CerfBltDescriptor*>(p);
        const bool ok = emu_.Get<CerfVirt::CerfVirtBlitter>().Execute(d);
        regs_[CerfVirt::kGpeCmdStatus / 4] =
            ok ? CerfVirt::kGpeStatusDone : CerfVirt::kGpeStatusError;
    }

    void ExecuteGradCmd() {
        regs_[CerfVirt::kGpeCmdStatus / 4] = CerfVirt::kGpeStatusBusy;
        const uint32_t off = regs_[CerfVirt::kGpeCmdDescVa / 4];

        uint8_t* p = emu_.Get<CerfVirtDmaArena>().At(
            off, (uint32_t)sizeof(CerfVirt::CerfGradDescriptor));
        if (!p) {
            LOG(Cerf, "[CerfVirtGpeCmd] gradient descriptor offset 0x%X outside arena\n",
                off);
            CerfFatalExit();
        }
        const CerfVirt::CerfGradDescriptor& d =
            *reinterpret_cast<const CerfVirt::CerfGradDescriptor*>(p);
        const bool ok = emu_.Get<CerfVirt::CerfVirtGradientFiller>().Execute(d);
        regs_[CerfVirt::kGpeCmdStatus / 4] =
            ok ? CerfVirt::kGpeStatusDone : CerfVirt::kGpeStatusError;
    }

    void ExecuteLineCmd() {
        regs_[CerfVirt::kGpeCmdStatus / 4] = CerfVirt::kGpeStatusBusy;
        const uint32_t off = regs_[CerfVirt::kGpeCmdDescVa / 4];

        uint8_t* p = emu_.Get<CerfVirtDmaArena>().At(
            off, (uint32_t)sizeof(CerfVirt::CerfLineDescriptor));
        if (!p) {
            LOG(Cerf, "[CerfVirtGpeCmd] line descriptor offset 0x%X outside arena\n",
                off);
            CerfFatalExit();
        }
        const CerfVirt::CerfLineDescriptor& d =
            *reinterpret_cast<const CerfVirt::CerfLineDescriptor*>(p);
        const bool ok = emu_.Get<CerfVirt::CerfVirtLineDrawer>().Execute(d);
        regs_[CerfVirt::kGpeCmdStatus / 4] =
            ok ? CerfVirt::kGpeStatusDone : CerfVirt::kGpeStatusError;
    }

    uint32_t regs_[4];
};

REGISTER_SERVICE(CerfVirtGpeCmd);

}

}
