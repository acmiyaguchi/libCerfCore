#include "../vr41xx/vr41xx_icu_impl.h"

#include <cstdint>
#include "vr4111_id.h"

namespace {

using cerf_vr41xx_icu_detail::Vr41xxIcuBase;
using cerf_vr41xx_icu_detail::Vr41xxIcuModel;

/* VR4111 ICU (UM Table 15-1 p328): SYSINT1REG..SOFTINTREG at 0x0B000080-0x0B00009A and
   SYSINT2REG..MFIRINTREG at 0x0B000200-0x0B00020A. UM Table 6-10 p170 decodes
   "0x0B00 009F to 0x0B00 0080 ICU1" and "0x0B00 021F to 0x0B00 0200 ICU2". */
constexpr Vr41xxIcuModel kModel = {
    /*base1=*/0x0B000080u,
    /*size1=*/0x20u,
    /*base2=*/0x0B000200u,
    /*size2=*/0x20u,
    /* SYSINT1REG bits with no Level-2 register (UM 15.2.1 p329 against the Level-2 fan-in
       of UM Figure 15-1 p327): D13 DOZEPIUINTR, D10 WRBERRINTR, D9 SIUINTR, D3 ETIMERINTR,
       D2 RTCL1INTR, D1 POWERINTR, D0 BATINTR. The rest arrive via SOFTINTREG (D11),
       GIUINTL/HREG (D8), KIUINTREG (D7), AIUINTREG (D6), PIUINTREG (D5). */
    /*s1_direct=*/0x260Fu,
    /* SYSINT2REG bits with no Level-2 register (UM 15.2.15 p345, same figure): D3
       TCLKINTR, D2 HSPINTR, D1 LEDINTR, D0 RTCL2INTR. D5 DSIUINTR comes from DSIUINTREG
       and D4 FIRINTR from FIRINTREG. */
    /*s2_direct=*/0x000Fu,
    /* DSIUINTREG D0: "Write 1 to this bit. 1 is returned after a read" (UM 15.2.6 p335). */
    /*dsiu_fixed_read=*/0x0001u,
    /* MSYSINT1REG D15, D14, D12, D4 Reserved (UM 15.2.7 p336). */
    /*msysint1_writable=*/0x2FEFu,
    /* MPIUINTREG D[15..7] and D1 Reserved (UM 15.2.8 p338). */
    /*mpiu_writable=*/0x007Du,
    /* MAIUINTREG D[15..12], D[7..4] and D0 Reserved (UM 15.2.9 p339). */
    /*maiu_writable=*/0x0F0Eu,
    /* MKIUINTREG D[15..3] Reserved (UM 15.2.10 p340). */
    /*mkiu_writable=*/0x0007u,
    /* MGIUINTLREG INTS[15..0] all R/W (UM 15.2.11 p341). */
    /*mgiul_writable=*/0xFFFFu,
    /* MDSIUINTREG D[15..12] and D[7..0] Reserved (UM 15.2.12 p342). */
    /*mdsiu_writable=*/0x0F00u,
    /* MSYSINT2REG D[15..6] Reserved (UM 15.2.18 p348). */
    /*msysint2_writable=*/0x003Fu,
    /* MGIUINTHREG INTS[31..16] all R/W (UM 15.2.19 p349). */
    /*mgiuh_writable=*/0xFFFFu,
    /* MFIRINTREG D[15..5] Reserved (UM 15.2.20 p350). */
    /*mfir_writable=*/0x001Fu,
};

class Vr4111Icu : public Vr41xxIcuBase<SocId::Vr4111, kModel> {
public:
    using Vr41xxIcuBase::Vr41xxIcuBase;
};

}  /* namespace */

REGISTER_SERVICE_AS(Vr4111Icu, Vr41xxIcu);
