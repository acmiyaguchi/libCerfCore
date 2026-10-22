#pragma once

#include <cstdint>

namespace cerfcore {

/* MCIMX28RM Rev. 2 Table 12-11: PERSISTENT1 [3:0] FORCE_RECOVERY, ROM_SECONDARY_BOOT,
   NAND_SDK_BLOCK_REWRITE, SD_SPEED_ENABLE are consumed by the boot ROM. */
constexpr uint32_t kP1BootRomBits = 0xFu;

/* Section 22.1: PERSISTENT1-5 "are completely software-defined". sharp_brain_pw_sh4 nk.exe
   OALIoCtl_101300c 8020a00c writes PERSISTENT1, OALIoCtl_1013014 8020a1c8 writes
   PERSISTENT2 with a caller value. */
inline const char* Imx28RtcPersistReject(uint32_t index, uint32_t old_value, uint32_t next) {
    switch (index) {
        case 1u:
            if (((old_value ^ next) & kP1BootRomBits) != 0u) {
                return "boot ROM bits [3:0] changed";
            }
            if ((next & ~kP1BootRomBits) != 0u) return "bits [31:4] set";
            return nullptr;
        case 2u:
            return nullptr;
        default:
            return "register the ROM never writes";
    }
}

}
