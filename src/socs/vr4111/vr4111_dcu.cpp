#include "../vr41xx/vr41xx_reg_window_impl.h"

#include <cstdint>
#include "vr4111_id.h"

namespace {

using cerf_vr41xx_reg_window_detail::ReadKind;
using cerf_vr41xx_reg_window_detail::Vr41xxRegWindowBase;
using cerf_vr41xx_reg_window_detail::Vr41xxRegWindowModel;
using cerf_vr41xx_reg_window_detail::WriteKind;

constexpr ReadKind  kRd = ReadKind::kStored;
constexpr WriteKind kWr = WriteKind::kStored;

/* VR4111 UM Table 6-10 p170: DCU decodes 0x0B000040-0x0B00005F. Table 13-2 p315 names the six
   registers and their R/W columns. Bit fields and both reset rows: DMASENREG 13.3.3 p318,
   DMAMSKREG 13.3.4 p319 (D15:4 and D1 reserved R, "0 is returned after a read"). */
/* Accessors: casio_cassiopeia_e55 nk.exe sub_9E816964 @0x9E8169BC DMASENREG=1, @0x9E8169B0
   DMAMSKREG=0; sub_9E81665C @0x9E81686C reads DMAMSKREG and @0x9E816870 branches on it;
   wavedev.dll sub_14B1804 read-modify-writes it via sub_14B39B8(pVRIO+70, 0xFFF7, 0). */
constexpr Vr41xxRegWindowModel kModel = {
    /*base=*/0x0B000040u,
    /*size=*/0x20u,
    /*num_regs=*/6u,
    /*word_pairs=*/false,
    {
        {},                               /* 0x40 DMARSTREG  R/W (Table 13-2 p315) */
        {},                               /* 0x42 DMAIDLEREG R   (Table 13-2 p315) */
        { kRd, kWr, 0x0001u, 0x0000u },   /* 0x44 DMASENREG  D0 DMASEN 1=enable 0=prohibit (13.3.3 p318) */
        { kRd, kWr, 0x000Du, 0x0000u },   /* 0x46 DMAMSKREG  D3 DMAMSKAIN D2 DMAMSKAOUT D0 DMAMSKFOUT (13.3.4 p319) */
        {},                               /* 0x48 DMAREQREG  R   (Table 13-2 p315) */
        {},                               /* 0x4A TDREG      R/W (Table 13-2 p315) */
    },
};

class Vr4111Dcu : public Vr41xxRegWindowBase<SocId::Vr4111, kModel> {
public:
    using Vr41xxRegWindowBase::Vr41xxRegWindowBase;
};

}

REGISTER_SERVICE(Vr4111Dcu);
