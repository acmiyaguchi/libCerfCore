#pragma once

#include "cerf_virt_addr_map.h"

namespace CerfVirt {

const uint32_t kArMagic   = 0xA070C0DEu;

const uint32_t kArPresent = 0x00u;
const uint32_t kArCount   = 0x04u;
const uint32_t kArEntries = 0x100u;

const uint32_t kArEntryWchars = 256u;
const uint32_t kArEntryStride = kArEntryWchars * 2u;
const uint32_t kArMaxEntries  = (kAutorunSize - kArEntries) / kArEntryStride;

}
