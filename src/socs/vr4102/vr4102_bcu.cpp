#include "../vr41xx/vr41xx_bcu_impl.h"

#include <cerfcore/core/cerf_emulator.h>
#include "vr4102_bcu_board.h"

#include <cstdint>
#include <vector>
#include "vr4102_id.h"

namespace {

using cerf_vr41xx_reg_window_detail::ReadKind;
using cerf_vr41xx_reg_window_detail::Vr41xxBcuBase;
using cerf_vr41xx_reg_window_detail::Vr41xxRegWindowModel;
using cerf_vr41xx_reg_window_detail::WriteKind;

/* VR4102 BCU block 0x0B000000-0x0B00001F (UM Table 5-10 / Table 10-1). */
constexpr Vr41xxRegWindowModel kModel = {
    /*base=*/0x0B000000u,
    /*size=*/0x20u,
    /*num_regs=*/10u,
    /*word_pairs=*/false,
    {
        /* 0x00 BCUCNTREG1 (UM 10.2.1, p236): reserved D11/9/7/5/3/2 read 0; RTCRST
           and Other-resets rows are 0. */
        { ReadKind::kStored, WriteKind::kStored, 0xF553u, 0x0000u, 0x0AACu },
        /* 0x02 BCUCNTREG2 (UM 10.2.2, p238): only GMODE (D0) is R/W; both rows 0. */
        { ReadKind::kStored, WriteKind::kStored, 0x0001u, 0x0000u, 0xFFFEu },
        {},
        {},
        {},
        {},
        {},
        {},
        {},
        /* 0x12 BCURFCOUNTREG (UM 10.2.7) is a live refresh down-counter that a write
           presets. nec_mobilepro_700_ce2 serial.dll sub_1580EE8 writes it then reads it
           into $zero (`lhu $zero,0($t7)`) to flush the write - the value is discarded.
           CERF models no DRAM refresh, so the idle count reads 0. */
        { ReadKind::kZero, WriteKind::kDrop, 0x0000u, 0x0000u, 0u },
    },
};

class Vr4102Bcu : public Vr41xxBcuBase<SocId::Vr4102, kModel> {
public:
    using Vr41xxBcuBase::Vr41xxBcuBase;

protected:
    std::vector<Vr41xxBcuBootWrite> KernelEntryWrites() const override {
        return emu_.Get<Vr4102BcuBoard>().KernelEntryWrites();
    }
};

}

REGISTER_SERVICE(Vr4102Bcu);
