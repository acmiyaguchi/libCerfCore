#include "../vr41xx/vr41xx_dsiu_impl.h"

#include <cstdint>
#include "vr4102_id.h"

namespace {

using cerf_vr41xx_dsiu_detail::Vr41xxDsiuBase;
using cerf_vr41xx_dsiu_detail::Vr41xxDsiuModel;

/* "0x0B00 01BF to 0x0B00 01A0: DSIU" (UM Table 5-10, Internal I/O Space 2). Its registers
   PORTREG..DSIURESETREG occupy 0x0B0001A0-0x0B0001B8 (UM Table 22-1). */
constexpr Vr41xxDsiuModel kModel = {
    /*base=*/0x0B0001A0u,
    /*size=*/0x20u,
    /* PORTREG Other-resets row: every bit 0 (UM 22.2.1). */
    /*portreg_retained_on_reset=*/false,
};

class Vr4102Dsiu : public Vr41xxDsiuBase<SocId::Vr4102, kModel> {
public:
    using Vr41xxDsiuBase::Vr41xxDsiuBase;
};

}  /* namespace */

REGISTER_SERVICE(Vr4102Dsiu);
