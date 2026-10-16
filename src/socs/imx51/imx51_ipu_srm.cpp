#include "imx51_ipu_srm.h"

void Imx51IpuSrm::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    regs_[off >> 2] = value;
}

REGISTER_SERVICE(Imx51IpuSrm);
