#include "imx51_ipu_internal_mem.h"

namespace {

/* TPM (IPU template/parameter memory) @ 0x5F060000, MCIMX51RM Table 42-1. */
class Imx51IpuTpm : public cerf_imx51_ipu_mem_detail::Imx51IpuInternalMem<0x5F060000u> {
    using Imx51IpuInternalMem::Imx51IpuInternalMem;
};

}  /* namespace */

REGISTER_SERVICE(Imx51IpuTpm);
