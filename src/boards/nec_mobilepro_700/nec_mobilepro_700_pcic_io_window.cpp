#include "nec_mobilepro_700_pcic.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_700_family_id.h"

#include <cstdint>

namespace {

constexpr uint32_t kBase = 0x14000000u;
constexpr uint32_t kSize = 0x00010000u;

constexpr uint32_t kPcicIndexOffset = 0x3E0u;
constexpr uint32_t kPcicDataOffset  = 0x3E1u;

class NecMobilePro700PcicIoWindow : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecMobilepro700;
    }

    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    /* Single serialization forward site for the owner Service (off the Periph
       walk); the mem-window peripheral deliberately does not duplicate it. */
    void SaveState(StateWriter& w) override {
        emu_.Get<NecMobilePro700Pcic>().SaveState(w);
    }
    void RestoreState(StateReader& r) override {
        emu_.Get<NecMobilePro700Pcic>().RestoreState(r);
    }
    void PostRestore() override {
        emu_.Get<NecMobilePro700Pcic>().PostRestore();
    }

    uint8_t ReadByte(uint32_t addr) override {
        const uint32_t off = addr - kBase;
        auto& pcic = emu_.Get<NecMobilePro700Pcic>();
        if (off == kPcicIndexOffset || off == kPcicDataOffset)
            return pcic.ReadPcicByte(off - kPcicIndexOffset);
        return pcic.ReadCardIo8(off);
    }
    uint16_t ReadHalf(uint32_t addr) override {
        const uint32_t off = addr - kBase;
        if (off == kPcicIndexOffset || off == kPcicDataOffset)
            HaltUnsupportedAccess("PCIC index/data ReadHalf", addr, 0);
        return emu_.Get<NecMobilePro700Pcic>().ReadCardIo16(off);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        const uint32_t off = addr - kBase;
        auto& pcic = emu_.Get<NecMobilePro700Pcic>();
        if (off == kPcicIndexOffset || off == kPcicDataOffset) {
            pcic.WritePcicByte(off - kPcicIndexOffset, value);
            return;
        }
        pcic.WriteCardIo8(off, value);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        const uint32_t off = addr - kBase;
        if (off == kPcicIndexOffset || off == kPcicDataOffset)
            HaltUnsupportedAccess("PCIC index/data WriteHalf", addr, value);
        emu_.Get<NecMobilePro700Pcic>().WriteCardIo16(off, value);
    }
};

}  /* namespace */

REGISTER_SERVICE(NecMobilePro700PcicIoWindow);
