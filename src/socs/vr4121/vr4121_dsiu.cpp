#include "../vr41xx/vr41xx_dsiu_impl.h"

#include <cstdint>
#include "vr4121_id.h"

namespace {

using cerf_vr41xx_dsiu_detail::Vr41xxDsiuBase;
using cerf_vr41xx_dsiu_detail::Vr41xxDsiuModel;

/* VR4121 UM Table 6-12 p178: DSIU decodes 0x0B0001A0-0x0B0001BF. Registers
   PORTREG..DSIURESETREG at 0x0B0001A0-0x0B0001B8 (UM Table 23-1); the RTC's TCLKLREG
   follows at 0x0B0001C0 (UM Table 1-7). */
constexpr Vr41xxDsiuModel kModel = {
    /*base=*/0x0B0001A0u,
    /*size=*/0x20u,
    /* PORTREG After-reset row: "Previous value is retained" (UM 23.2.1). */
    /*portreg_retained_on_reset=*/true,
};

class Vr4121Dsiu : public Vr41xxDsiuBase<SocId::Vr4121, kModel> {
public:
    using Vr41xxDsiuBase::Vr41xxDsiuBase;
};

}  /* namespace */

REGISTER_SERVICE(Vr4121Dsiu);
