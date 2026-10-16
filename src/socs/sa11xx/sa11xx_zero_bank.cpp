#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "sa1110_id.h"
#include "sa1100_id.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/cpu/emulated_memory.h>

namespace cerfcore {

namespace {

/* SA-1110 Developer's Manual §2.4 Figure 2-3 and SA-1100 Technical
   Reference Manual §2.4 Figure 2-3: Zeros Bank, 128 Mbyte at PA
   0xE0000000, reads return zero, writes have no effect. */

class Sa11xxZeroBank : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && (bd->GetSocId() == SocId::Sa1110 || bd->GetSocId() == SocId::Sa1100);
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<EmulatedMemory>().AddRegion(
            MmioBase(), EmulatedMemory::PAGE_SIZE, MemoryKind::kFlash, MmioSize());
    }

    uint32_t MmioBase() const override { return 0xE0000000u; }
    uint32_t MmioSize() const override { return 0x08000000u; }

    uint8_t  ReadByte (uint32_t)              override { return 0; }
    uint16_t ReadHalf (uint32_t)              override { return 0; }
    uint32_t ReadWord (uint32_t)              override { return 0; }
    void     WriteByte(uint32_t, uint8_t)     override {}
    void     WriteHalf(uint32_t, uint16_t)    override {}
    void     WriteWord(uint32_t, uint32_t)    override {}
};

}  /* namespace */

REGISTER_SERVICE(Sa11xxZeroBank);

}
