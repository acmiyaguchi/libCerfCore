#include "msm8255_sdcc_restored_register.h"

#include "msm8255_sdcc_regs.h"

namespace cerf_msm8255_sdcc_detail {

uint32_t ReadRestoredRegister(StateReader& r, const char* name, uint32_t writable,
                              uint32_t base, uint32_t offset) {
    uint32_t value = kUngroundedPowerOn;
    r.Read(name, value);
    if ((value & ~writable) != 0u) {
        r.Reject(
            "Peripheral at 0x%08X: restored +0x%03X value 0x%08X carries "
            "bits the guest never writes", base, offset, value);
    }
    return value;
}

}  // namespace cerf_msm8255_sdcc_detail
