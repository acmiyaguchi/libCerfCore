#pragma once

#include "rom_image_parse.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace cerfcore {

namespace cerf::rom_image_parse {

/* sharp_brain_pw_sh4 SDU item 0 Update.exe 00017488, 00017214, 00017870 */
constexpr uint32_t kSharpSduHeaderBytes   = 0x8000u;
constexpr size_t   kSharpSduChecksumOff   = 0x04u;
constexpr size_t   kSharpSduFileSizeOff   = 0x08u;
constexpr size_t   kSharpSduItemCountOff  = 0x18u;
constexpr size_t   kSharpSduItemTableOff  = 0x7Cu;
constexpr size_t   kSharpSduItemEntrySize = 0x1Cu;

/* sharp_brain_pw_sh4 SDU item 0 Update.exe 00013e6c */
constexpr uint32_t kSharpSduOsItem        = 3u;
constexpr uint32_t kSharpSduBootStreamBytes = 0x7D000u;

struct SharpSduOsXip {
    size_t   data_off  = 0;
    uint32_t flat_size = 0;
    uint32_t base_va   = 0;
};

bool SharpSduHeaderValid(std::span<const uint8_t> raw);

bool SharpSduLocateOsXip(std::span<const uint8_t> raw, SharpSduOsXip& out);

}  /* namespace cerf::rom_image_parse */

}
