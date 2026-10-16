#include "imx51_ipu_internal_mem.h"

namespace {

/* DC template memory @ 0x5F080000, MCIMX51RM Table 42-1 (the Display Controller's
   microcode/template RAM, distinct from the DC register submodule at 0x5E058000). */
class Imx51IpuDcTemplate : public cerf_imx51_ipu_mem_detail::Imx51IpuInternalMem<0x5F080000u> {
    using Imx51IpuInternalMem::Imx51IpuInternalMem;
};

}  /* namespace */

REGISTER_SERVICE(Imx51IpuDcTemplate);
