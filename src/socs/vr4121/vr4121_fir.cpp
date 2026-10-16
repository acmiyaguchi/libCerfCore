#include "../vr41xx/vr41xx_fir_impl.h"
#include "vr4121_id.h"

namespace {

/* VR4121 FIR base, Internal I/O Space 1 0x0C000040-0x0C000075 (VR4121 UM Table 27-1 p588). */
class Vr4121Fir : public cerf_vr41xx_fir_detail::Vr41xxFirBase<SocId::Vr4121, 0x0C000040u> {
public:
    using Vr41xxFirBase::Vr41xxFirBase;
};

}  /* namespace */

REGISTER_SERVICE(Vr4121Fir);
