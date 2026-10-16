#include "../vr41xx/vr41xx_reg_window_impl.h"

#include <cstdint>
#include "vr4102_id.h"

namespace {

using cerf_vr41xx_reg_window_detail::ReadKind;
using cerf_vr41xx_reg_window_detail::Vr41xxRegWindowBase;
using cerf_vr41xx_reg_window_detail::Vr41xxRegWindowModel;
using cerf_vr41xx_reg_window_detail::WriteKind;

constexpr ReadKind  kRd = ReadKind::kStored;
constexpr WriteKind kWr = WriteKind::kStored;

/* DCU 0x0B000040-0x0B00005F (VR4102 UM Table 12-2); per-register bits and reset rows
   (all 0) in VR4102 UM 12.3.1-12.3.6.
   DMAREQREG 0x48 D3/D2/D0 read the AIU/FIR request lines and CERF posts no DMA
   request, so it reads 0 ("R" for every bit, UM 12.3.5). */
constexpr Vr41xxRegWindowModel kModel = {
    /*base=*/0x0B000040u,
    /*size=*/0x20u,
    /*num_regs=*/6u,
    /*word_pairs=*/false,
    {
        { kRd, kWr, 0x0001u, 0x0000u },   /* 0x40 DMARSTREG  D0 DMARST 0=reset 1=normal (12.3.1) */
        {},                               /* 0x42 DMAIDLEREG D0 DMAISTAT 1=idle 0=busy, R (12.3.2) */
        { kRd, kWr, 0x0001u, 0x0000u },   /* 0x44 DMASENREG  D0 DMASEN sequencer enable (12.3.3) */
        { kRd, kWr, 0x000Du, 0x0000u },   /* 0x46 DMAMSKREG  D3 DMAMSKAIN D2 DMAMSKAOUT D0 DMAMSKFOUT (12.3.4) */
        { ReadKind::kZero, WriteKind::kDrop, 0x0000u, 0x0000u },
                                          /* 0x48 DMAREQREG  D3/D2/D0 request pending, R (12.3.5) */
        { kRd, kWr, 0x0001u, 0x0000u },   /* 0x4A TDREG      D0 FIR transfer direction (12.3.6) */
    },
};

class Vr4102Dcu : public Vr41xxRegWindowBase<SocId::Vr4102, kModel> {
public:
    using Vr41xxRegWindowBase::Vr41xxRegWindowBase;
};

}

REGISTER_SERVICE(Vr4102Dcu);
