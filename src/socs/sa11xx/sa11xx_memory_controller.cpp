#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "sa1110_id.h"
#include "sa1100_id.h"
#include "sa1110_sdram_decode.h"
#include "sa11xx_bootloader_memory_setup.h"
#include "sa11xx_memory_register_map.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>

namespace {

class Sa11xxMemoryController : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && (bd->GetSocId() == SocId::Sa1110 || bd->GetSocId() == SocId::Sa1100);
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        map_          = &emu_.Get<Sa11xxMemoryRegisterMap>();
        sdram_decode_ = emu_.TryGet<Sa1110SdramDecode>();
        boot_setup_   = emu_.TryGet<Sa11xxBootloaderMemorySetup>();
        map_->ApplyResetValues(regs_);
        ApplyBootloaderSetup();
        ApplyMdcnfg();
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
            if (kind == ResetLineKind::Rtc ||
                emu_.Get<GuestCpuReset>().DeliveredResetWasResume())
                map_->ApplyResetValues(regs_);
            ApplyBootloaderSetup();
            ApplyMdcnfg();
        });
    }

    uint32_t MmioBase() const override { return 0xA0000000u; }
    uint32_t MmioSize() const override { return 0x00010000u; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override    { w.WriteBytes("regs", regs_, sizeof(regs_)); }
    void RestoreState(StateReader& r) override { r.ReadBytes("regs", regs_, sizeof(regs_)); }
    void PostRestore() override { ApplyMdcnfg(); }

private:
    uint32_t regs_[Sa11xxMemoryRegisterMap::kMaxRegisters] = {};
    const Sa11xxMemoryRegisterMap* map_ = nullptr;
    Sa1110SdramDecode* sdram_decode_ = nullptr;
    const Sa11xxBootloaderMemorySetup* boot_setup_ = nullptr;

    void ApplyMdcnfg() {
        if (sdram_decode_) sdram_decode_->ApplyMdcnfg(regs_[0]);
    }

    void ApplyBootloaderSetup() {
        if (boot_setup_) regs_[0] = boot_setup_->Mdcnfg();
    }

    bool OffsetToIndex(uint32_t off, uint32_t* index_out) const {
        if (off >= map_->RegisterCount() * 4u || (off & 0x3u) != 0) return false;
        *index_out = off / 4u;
        return true;
    }
};

uint8_t Sa11xxMemoryController::ReadByte(uint32_t addr) {
    const uint32_t off   = addr - MmioBase();
    const uint32_t base  = off & ~0x3u;
    const uint32_t shift = (off & 0x3u) * 8;
    uint32_t index;
    if (!OffsetToIndex(base, &index)) HaltUnsupportedAccess("ReadByte", addr, 0);
    return static_cast<uint8_t>((regs_[index] >> shift) & 0xFFu);
}

uint32_t Sa11xxMemoryController::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    uint32_t index;
    if (!OffsetToIndex(off, &index)) HaltUnsupportedAccess("ReadWord", addr, 0);
    return regs_[index];
}

void Sa11xxMemoryController::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off   = addr - MmioBase();
    const uint32_t base  = off & ~0x3u;
    const uint32_t shift = (off & 0x3u) * 8;
    uint32_t index;
    if (!OffsetToIndex(base, &index)) HaltUnsupportedAccess("WriteByte", addr, value);
    const uint32_t cur     = regs_[index];
    const uint32_t cleared = cur & ~(0xFFu << shift);
    regs_[index] = cleared | (static_cast<uint32_t>(value) << shift);
    if (index == 0u) ApplyMdcnfg();
}

void Sa11xxMemoryController::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    uint32_t index;
    if (!OffsetToIndex(off, &index)) HaltUnsupportedAccess("WriteWord", addr, value);
    regs_[index] = value;
    if (index == 0u) ApplyMdcnfg();
}

}

REGISTER_SERVICE(Sa11xxMemoryController);
