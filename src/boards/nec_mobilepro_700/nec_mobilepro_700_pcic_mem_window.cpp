#include "nec_mobilepro_700_pcic.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_700_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* VR4102 ISA memory space (Vr4102-um Tbl 5-6/5-9/5-10). The two PCMCIA sockets'
   ExCA MEM_MAP windows decode within it: the guest's socket-0 CIS window maps
   host_page 0x210 (PA 0x10210000) to card attribute address 0. 16 MB spans the
   82365 12-bit page window (no WINDOW_PAGE writes -> mem_page 0). */
constexpr uint32_t kBase = 0x10000000u;
constexpr uint32_t kSize = 0x01000000u;

class NecMobilePro700PcicMemWindow : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecMobilepro700;
    }

    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint8_t ReadByte(uint32_t addr) override {
        return emu_.Get<NecMobilePro700Pcic>().ReadCardMem8(addr - kBase);
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return emu_.Get<NecMobilePro700Pcic>().ReadCardMem16(addr - kBase);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        emu_.Get<NecMobilePro700Pcic>().WriteCardMem8(addr - kBase, value);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        emu_.Get<NecMobilePro700Pcic>().WriteCardMem16(addr - kBase, value);
    }
};

}  /* namespace */

REGISTER_SERVICE(NecMobilePro700PcicMemWindow);

}
