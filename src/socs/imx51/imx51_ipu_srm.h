#pragma once

#include "imx51_ipu_internal_mem.h"

#include <cstdint>

/* IPU CM-Shadow / SRM window @ 0x5F040000 (MCIMX51RM Table 42-1); holds the DP
   synchronous-flow composition registers the guest programs for FG-over-BG. */
class Imx51IpuSrm : public cerf_imx51_ipu_mem_detail::Imx51IpuInternalMem<0x5F040000u> {
public:
    using Imx51IpuInternalMem::Imx51IpuInternalMem;

    /* DP_COM_CONF_SYNC @ +0x00 (RM Table 42-116): FG_EN[0] GWSEL[1] GWAM[2](0=local,1=global alpha). */
    uint32_t DpComConfSync() const { return regs_[0x00u >> 2]; }
    /* DP_GRAPH_WIND_CTRL_SYNC @ +0x04 (RM Table/Fig 42-117): GWAV[31:24] global alpha
       (0x00 = FG totally opaque overlay), GWCKR/G/B[23:0] color-key. */
    uint32_t DpGraphWindCtrlSync() const { return regs_[0x04u >> 2]; }
    /* DP_FG_POS_SYNC @ +0x08: FG graphic-window origin (x,y); 0 = at frame origin. */
    uint32_t DpFgPosSync() const { return regs_[0x08u >> 2]; }

    void WriteWord(uint32_t addr, uint32_t value) override;
};
