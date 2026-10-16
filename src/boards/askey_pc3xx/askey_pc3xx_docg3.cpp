#include "../../peripherals/msystems_docg3/msystems_docg3_base.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "askey_pc3xx_family_id.h"

#include <cstddef>
#include <cstdint>

namespace {

/* The DiskOnChip G3 is the Falcon's CS0 boot device: registry WindowBase=0 and
   the OAL's uncached probe at 0xB8300000 (nk.exe sub_800F3FFC) both resolve to
   PA 0 - OAT VA 0x98300000 -> PA 0, +0x20000000 is its uncached alias. */
class AskeyPc3xxDocG3 : public MsystemsDocG3Base {
public:
    using MsystemsDocG3Base::MsystemsDocG3Base;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::AskeyPc3xx;
    }

    uint32_t WindowPa()   const override { return 0x00000000u; }
    uint32_t BlockCount() const override { return 1024u; }
    void     LoadInto(uint8_t*, std::size_t) override {}   /* blank until provisioned */
};

}  /* namespace */

REGISTER_SERVICE_AS(AskeyPc3xxDocG3, MsystemsDocG3Base);
