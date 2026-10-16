#pragma once

#if defined(_MSC_VER) && _MSC_VER < 1600
typedef unsigned int uint32_t;
#else
#include <cstdint>
#endif

namespace CerfVirt {

const uint32_t kNicMagic = 0xCEF0E7C0u;

const uint32_t kNicRegMagic      = 0x00u;
const uint32_t kNicRegMacWord0   = 0x08u;
const uint32_t kNicRegMacWord1   = 0x0Cu;
const uint32_t kNicRegLinkUp     = 0x10u;
const uint32_t kNicRegMaxFrame   = 0x14u;

const uint32_t kNicRegTxWriteSeq = 0x20u;
const uint32_t kNicRegTxReadSeq  = 0x24u;
const uint32_t kNicRegRxWriteSeq = 0x28u;
const uint32_t kNicRegRxReadSeq  = 0x2Cu;

const uint32_t kNicRegRxDropped  = 0x30u;

const uint32_t kNicSlotLenOff     = 0x00u;
const uint32_t kNicSlotPayloadOff = 0x04u;

}
