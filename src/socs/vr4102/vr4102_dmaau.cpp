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

/* VR4102 UM Table 5-10 p139: DMAAU decodes 0x0B000020-0x0B00003F; Table 11-1 lists its
   registers at 0x0B000020-0x0B000037. AIU base-low D0 read-only (UM 11.2.1/11.2.3); AIU
   address-low and both FIR low halves all-16 R/W (UM 11.2.2/11.2.5/11.2.6); high halves
   writable D[8:0]. Low reset 0xF800, high 0x01FF; RTCRST == Other resets (UM 11.2.1-11.2.6). */
constexpr Vr41xxRegWindowModel kModel = {
    /*base=*/0x0B000020u,
    /*size=*/0x20u,
    /*num_regs=*/12u,
    /*word_pairs=*/true,
    {
        { kRd, kWr, 0xFFFEu, 0xF800u },   /* AIUIBALREG 0x20 */
        { kRd, kWr, 0x01FFu, 0x01FFu },   /* AIUIBAHREG 0x22 */
        { kRd, kWr, 0xFFFFu, 0xF800u },   /* AIUIALREG  0x24 */
        { kRd, kWr, 0x01FFu, 0x01FFu },   /* AIUIAHREG  0x26 */
        { kRd, kWr, 0xFFFEu, 0xF800u },   /* AIUOBALREG 0x28 */
        { kRd, kWr, 0x01FFu, 0x01FFu },   /* AIUOBAHREG 0x2A */
        { kRd, kWr, 0xFFFFu, 0xF800u },   /* AIUOALREG  0x2C */
        { kRd, kWr, 0x01FFu, 0x01FFu },   /* AIUOAHREG  0x2E */
        { kRd, kWr, 0xFFFFu, 0xF800u },   /* FIRBALREG  0x30 */
        { kRd, kWr, 0x01FFu, 0x01FFu },   /* FIRBAHREG  0x32 */
        { kRd, kWr, 0xFFFFu, 0xF800u },   /* FIRALREG   0x34 */
        { kRd, kWr, 0x01FFu, 0x01FFu },   /* FIRAHREG   0x36 */
    },
};

class Vr4102Dmaau : public Vr41xxRegWindowBase<SocId::Vr4102, kModel> {
public:
    using Vr41xxRegWindowBase::Vr41xxRegWindowBase;
};

}

REGISTER_SERVICE(Vr4102Dmaau);
