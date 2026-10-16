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

/* RAM + I/O-space window 0x0F0001E0-0x1EF (VR4131 UM Table 8-1, p162). Write masks +
   reset values per register: RAMBAL/H 8.2.7 p169, RAMAL/H 8.2.8 p170, IOBAL/H 8.2.9
   p171 (IOBAH D11 "Write 1. 1 is returned"), IOAL/H 8.2.10 p172. */
constexpr Vr41xxRegWindowModel kModel = {
    /*base=*/0x0F0001E0u,
    /*size=*/0x10u,
    /*num_regs=*/8u,
    /*word_pairs=*/true,
    {
        { kRd, kWr, 0xFFFCu, 0xF800u, 0u },   /* RAMBAL 15:2 */
        { kRd, kWr, 0x07FFu, 0x01FFu, 0u },   /* RAMBAH 10:0 */
        { kRd, kWr, 0xFFFCu, 0xF800u, 0u },   /* RAMAL 15:2  */
        { kRd, kWr, 0x0003u, 0x01FFu, 0u },   /* RAMAH 1:0   */
        { kRd, kWr, 0xFFFCu, 0x0000u, 0u },   /* IOBAL 15:2  */
        { kRd, kWr, 0x07FFu, 0x0A00u, 0u },   /* IOBAH 10:0  */
        { kRd, kWr, 0xFFFCu, 0x0000u, 0u },   /* IOAL 15:2   */
        { kRd, kWr, 0x07FFu, 0x0AFFu, 0u },   /* IOAH 10:0   */
    },
};

class Vr4122DmaauRamIo : public Vr41xxRegWindowBase<SocId::Vr4122, kModel> {
public:
    using Vr41xxRegWindowBase::Vr41xxRegWindowBase;
};

}

REGISTER_SERVICE(Vr4122DmaauRamIo);
