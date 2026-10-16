#include "../vr41xx/vr41xx_fir_impl.h"
#include "vr4102_id.h"

namespace {

/* VR4102 FIR base, Internal I/O Space 1 0x0C000040-0x0C000075 (VR4102 UM Table 26-1 p498). */
class Vr4102Fir : public cerf_vr41xx_fir_detail::Vr41xxFirBase<SocId::Vr4102, 0x0C000040u> {
public:
    using Vr41xxFirBase::Vr41xxFirBase;
};

}  /* namespace */

REGISTER_SERVICE(Vr4102Fir);
