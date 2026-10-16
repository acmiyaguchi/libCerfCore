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

/* CSI + FIR window 0x0F000020-0x37 (VR4131 UM Table 8-1, p162). Write masks + reset
   values per register: CSIIBAL/H 8.2.1 p163, CSIIAL/H 8.2.2 p164, CSIOBAL/H 8.2.3
   p165, CSIOAL/H 8.2.4 p166, FIRBAL/H 8.2.5 p167, FIRAL/H 8.2.6 p168. Boot writes
   FIRBAL/H at nk.exe 0x9F0338EC (sw 0xA0003800 -> 0x0F000030). */
constexpr Vr41xxRegWindowModel kModel = {
    /*base=*/0x0F000020u,
    /*size=*/0x20u,
    /*num_regs=*/12u,
    /*word_pairs=*/true,
    {
        { kRd, kWr, 0xFFFCu, 0xF800u, 0u },   /* CSIIBAL 15:2  */
        { kRd, kWr, 0x07FFu, 0x01FFu, 0u },   /* CSIIBAH 10:0  */
        { kRd, kWr, 0x07FCu, 0xF800u, 0u },   /* CSIIAL 10:2   */
        { kRd, kWr, 0x0000u, 0x01FFu, 0u },   /* CSIIAH R      */
        { kRd, kWr, 0xFFFFu, 0xF800u, 0u },   /* CSIOBAL 15:0  */
        { kRd, kWr, 0x07FFu, 0x01FFu, 0u },   /* CSIOBAH 10:0  */
        { kRd, kWr, 0x07FCu, 0xF800u, 0u },   /* CSIOAL 10:2   */
        { kRd, kWr, 0x0000u, 0x01FFu, 0u },   /* CSIOAH R      */
        { kRd, kWr, 0xFFFFu, 0xF800u, 0u },   /* FIRBAL 15:0   */
        { kRd, kWr, 0x07FFu, 0x01FFu, 0u },   /* FIRBAH 10:0   */
        { kRd, kWr, 0x0FFFu, 0xF800u, 0u },   /* FIRAL 11:0    */
        { kRd, kWr, 0x0000u, 0x01FFu, 0u },   /* FIRAH R       */
    },
};

class Vr4122Dmaau : public Vr41xxRegWindowBase<SocId::Vr4122, kModel> {
public:
    using Vr41xxRegWindowBase::Vr41xxRegWindowBase;
};

}

REGISTER_SERVICE(Vr4122Dmaau);
