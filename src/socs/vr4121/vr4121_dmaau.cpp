#include "../vr41xx/vr41xx_reg_window_impl.h"

#include <cstdint>
#include "vr4121_id.h"

namespace {

using cerf_vr41xx_reg_window_detail::ReadKind;
using cerf_vr41xx_reg_window_detail::Vr41xxRegWindowBase;
using cerf_vr41xx_reg_window_detail::Vr41xxRegWindowModel;
using cerf_vr41xx_reg_window_detail::WriteKind;

constexpr ReadKind  kRd = ReadKind::kStored;
constexpr WriteKind kWr = WriteKind::kStored;

/* VR4121 UM Table 6-12 p178: DMAAU decodes 0x0B000020-0x0B00003F; Table 12-1 lists its
   registers at 0x0B000020-0x0B000037. AIU base-low D0 read-only (UM 12.2.1/12.2.3); AIU
   address-low and both FIR low halves all-16 R/W (UM 12.2.2/12.2.5/12.2.6); high halves
   writable D[10:0]. Low reset 0xF800, high 0x01FF; RTCRST == After-reset (UM 12.2.1-12.2.6). */
constexpr Vr41xxRegWindowModel kModel = {
    /*base=*/0x0B000020u,
    /*size=*/0x20u,
    /*num_regs=*/12u,
    /*word_pairs=*/true,
    {
        { kRd, kWr, 0xFFFEu, 0xF800u },   /* AIUIBALREG 0x20 */
        { kRd, kWr, 0x07FFu, 0x01FFu },   /* AIUIBAHREG 0x22 */
        { kRd, kWr, 0xFFFFu, 0xF800u },   /* AIUIALREG  0x24 */
        { kRd, kWr, 0x07FFu, 0x01FFu },   /* AIUIAHREG  0x26 */
        { kRd, kWr, 0xFFFEu, 0xF800u },   /* AIUOBALREG 0x28 */
        { kRd, kWr, 0x07FFu, 0x01FFu },   /* AIUOBAHREG 0x2A */
        { kRd, kWr, 0xFFFFu, 0xF800u },   /* AIUOALREG  0x2C */
        { kRd, kWr, 0x07FFu, 0x01FFu },   /* AIUOAHREG  0x2E */
        { kRd, kWr, 0xFFFFu, 0xF800u },   /* FIRBALREG  0x30 */
        { kRd, kWr, 0x07FFu, 0x01FFu },   /* FIRBAHREG  0x32 */
        { kRd, kWr, 0xFFFFu, 0xF800u },   /* FIRALREG   0x34 */
        { kRd, kWr, 0x07FFu, 0x01FFu },   /* FIRAHREG   0x36 */
    },
};

class Vr4121Dmaau : public Vr41xxRegWindowBase<SocId::Vr4121, kModel> {
public:
    using Vr41xxRegWindowBase::Vr41xxRegWindowBase;
};

}

REGISTER_SERVICE(Vr4121Dmaau);
