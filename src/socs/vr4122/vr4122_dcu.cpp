#include "../vr41xx/vr41xx_reg_window_impl.h"

#include <cstdint>
#include "vr4122_id.h"

namespace {

using cerf_vr41xx_reg_window_detail::ReadKind;
using cerf_vr41xx_reg_window_detail::Vr41xxRegWindowBase;
using cerf_vr41xx_reg_window_detail::Vr41xxRegWindowModel;
using cerf_vr41xx_reg_window_detail::WriteKind;

constexpr ReadKind  kRd = ReadKind::kStored;
constexpr WriteKind kWr = WriteKind::kStored;

/* VR4131 UM U15350EJ2V0UM Table 9-2 p173; masks + reset columns 9.3.1-9.3.13
   p174-183 (RTCRST == After-reset; DMAIDLEREG resets to 1). "Setting the DRQIOR
   bit to 1 starts DMA transfer" (9.3.5 p177); TCINT "is transmitted as BCUINT to
   the ICU" (9.3.13 p183) - both stay FATAL while the transfer engine is unmodeled. */
constexpr Vr41xxRegWindowModel kModel = {
    /*base=*/0x0F000040u,
    /*size=*/0x20u,
    /*num_regs=*/13u,
    /*word_pairs=*/true,
    {
        { kRd, kWr, 0x0001u, 0x0000u, 0x0000u },  /* 0x40 DMARSTREG  (9.3.1)  */
        { kRd, kWr, 0x0000u, 0x0001u, 0x0000u },  /* 0x42 DMAIDLEREG (9.3.2)  */
        { kRd, kWr, 0x0001u, 0x0000u, 0x0000u },  /* 0x44 DMASENREG  (9.3.3)  */
        { kRd, kWr, 0x000Fu, 0x0000u, 0x0000u },  /* 0x46 DMAMSKREG  (9.3.4)  */
        /* 0x48 DMAREQREG (9.3.5): D3 DRQIOR R/W, 2:0 R; DRQIOR=1 starts an
           unmodeled DMA transfer. */
        { kRd, kWr, 0x0008u, 0x0000u, 0x0008u },
        { kRd, kWr, 0x0003u, 0x0000u, 0x0000u },  /* 0x4A TDREG      (9.3.6)  */
        { kRd, kWr, 0x000Fu, 0x0000u, 0x0000u },  /* 0x4C DMAABITREG (9.3.7)  */
        { kRd, kWr, 0x000Fu, 0x0000u, 0x0000u },  /* 0x4E CONTROLREG (9.3.8)  */
        { kRd, kWr, 0xFFFCu, 0x0000u, 0x0000u },  /* 0x50 BASSCNTLREG    (9.3.9)  */
        { kRd, kWr, 0x0003u, 0x0000u, 0x0000u },  /* 0x52 BASSCNTHREG    (9.3.10) */
        { kRd, kWr, 0xFFFCu, 0x0000u, 0x0000u },  /* 0x54 CURRENTCNTLREG (9.3.11) */
        { kRd, kWr, 0x0003u, 0x0000u, 0x0000u },  /* 0x56 CURRENTCNTHREG (9.3.12) */
        /* 0x58 TCINTREG (9.3.13): D0 TCINT=1 pends an unmodeled BCUINT. */
        { kRd, kWr, 0x0001u, 0x0000u, 0x0001u },
    },
};

class Vr4122Dcu : public Vr41xxRegWindowBase<SocId::Vr4122, kModel> {
public:
    using Vr41xxRegWindowBase::Vr41xxRegWindowBase;
};

}

REGISTER_SERVICE(Vr4122Dcu);
