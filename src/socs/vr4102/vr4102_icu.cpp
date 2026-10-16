#include "../vr41xx/vr41xx_icu_impl.h"

#include <cstdint>
#include "vr4102_id.h"

namespace {

using cerf_vr41xx_icu_detail::Vr41xxIcuBase;
using cerf_vr41xx_icu_detail::Vr41xxIcuModel;

/* VR4102 ICU (UM Table 14-1): SYSINT1REG..SOFTINTREG at 0x0B000080-0x0B00009A and
   SYSINT2REG..MFIRINTREG at 0x0B000200-0x0B00020A. UM Table 5-10 p139 decodes
   "0x0B00 009F to 0x0B00 0080 ICU1" and "0x0B00 021F to 0x0B00 0200 ICU2". */
constexpr Vr41xxIcuModel kModel = {
    /*base1=*/0x0B000080u,
    /*size1=*/0x20u,
    /*base2=*/0x0B000200u,
    /*size2=*/0x20u,
    /* SYSINT1REG bits with no Level-2 register (UM 14.2.1): D13 DOZEPIUINTR,
       D10 WRBERRINTR, D9 SIUINTR, D3 ETIMERINTR, D2 RTCL1INTR, D1 POWERINTR,
       D0 BATINTR. D11 SOFTINTR is NOT one - it is computed from SOFTINTREG. */
    /*s1_direct=*/0x260Fu,
    /* SYSINT2REG bits with no Level-2 register (UM 14.2.15): D3 TCLKINTR, D2 HSPINTR,
       D1 LEDINTR, D0 RTCL2INTR. */
    /*s2_direct=*/0x000Fu,
    /* DSIUINTREG D0: "Write 1 to this bit. 1 is returned after a read" (UM 14.2.6). */
    /*dsiu_fixed_read=*/0x0001u,
    /* MSYSINT1REG D15, D14, D12, D4 Reserved (UM 14.2.7 p302). */
    /*msysint1_writable=*/0x2FEFu,
    /* MPIUINTREG D[15..7] and D1 Reserved (UM 14.2.8 p304). */
    /*mpiu_writable=*/0x007Du,
    /* MAIUINTREG D[15..12], D[7..4] and D0 Reserved (UM 14.2.9 p305). */
    /*maiu_writable=*/0x0F0Eu,
    /* MKIUINTREG D[15..3] Reserved (UM 14.2.10 p306). */
    /*mkiu_writable=*/0x0007u,
    /* MGIUINTLREG INTS[15..0] all R/W (UM 14.2.11 p307). */
    /*mgiul_writable=*/0xFFFFu,
    /* MDSIUINTREG D[15..12] and D[7..0] Reserved (UM 14.2.12 p308). */
    /*mdsiu_writable=*/0x0F00u,
    /* MSYSINT2REG D[15..6] Reserved (UM 14.2.18 p314). */
    /*msysint2_writable=*/0x003Fu,
    /* MGIUINTHREG INTS[31..16] all R/W (UM 14.2.19 p315). */
    /*mgiuh_writable=*/0xFFFFu,
    /* MFIRINTREG D[15..5] Reserved (UM 14.2.20 p316). */
    /*mfir_writable=*/0x001Fu,
};

class Vr4102Icu : public Vr41xxIcuBase<SocId::Vr4102, kModel> {
public:
    using Vr41xxIcuBase::Vr41xxIcuBase;
};

}  /* namespace */

REGISTER_SERVICE_AS(Vr4102Icu, Vr41xxIcu);
