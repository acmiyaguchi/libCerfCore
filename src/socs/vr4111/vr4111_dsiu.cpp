#include "../vr41xx/vr41xx_dsiu_impl.h"

#include <cstdint>
#include "vr4111_id.h"

namespace {

using cerf_vr41xx_dsiu_detail::Vr41xxDsiuBase;
using cerf_vr41xx_dsiu_detail::Vr41xxDsiuModel;

/* VR4111 UM Table 6-10 p170: DSIU decodes 0x0B0001A0-0x0B0001BF. Table 23-1 p477 lists
   PORTREG..DSIURESETREG at 0x0B0001A0-0x0B0001B8. */
constexpr Vr41xxDsiuModel kModel = {
    /*base=*/0x0B0001A0u,
    /*size=*/0x20u,
    /* PORTREG Other-resets row: "Previous value is retained" (UM 23.2.1 p478). */
    /*portreg_retained_on_reset=*/true,
};

class Vr4111Dsiu : public Vr41xxDsiuBase<SocId::Vr4111, kModel> {
public:
    using Vr41xxDsiuBase::Vr41xxDsiuBase;
};

}  /* namespace */

REGISTER_SERVICE(Vr4111Dsiu);
