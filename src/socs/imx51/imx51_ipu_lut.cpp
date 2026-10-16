#include "imx51_ipu_internal_mem.h"

namespace {

/* LUT (IPU lookup-table memory) @ 0x5F020000, MCIMX51RM Table 42-1. */
class Imx51IpuLut : public cerf_imx51_ipu_mem_detail::Imx51IpuInternalMem<0x5F020000u> {
    using Imx51IpuInternalMem::Imx51IpuInternalMem;
};

}  /* namespace */

REGISTER_SERVICE(Imx51IpuLut);
