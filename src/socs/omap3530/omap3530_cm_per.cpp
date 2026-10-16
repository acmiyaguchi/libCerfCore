#include "omap3530_cm_per.h"

namespace cerfcore {

namespace {

/* OMAP35x TRM SPRUF98Y Table 4-237 CM_FCLKEN_PER (page 490) and Table 4-239 CM_ICLKEN_PER (page 492):
   EN_MCBSP2 [0], EN_MCBSP3 [1], EN_MCBSP4 [2]. */
constexpr uint32_t kMcBspBits = 0x7u;

/* SPRUF98Y Table 4-236 (page 490): Reset Type W at 0x00, 0x10, 0x30, 0x40, 0x44, 0x48; every field resets to 0 in
   Table 4-237 (page 491), Table 4-239 (page 493), Table 4-243 (page 496), Table 4-245 (page 497), Table 4-247 (page 498),
   Table 4-249 (page 499). */
constexpr Omap3530RegReset kWarmReset[] = {{0x00u, 0u}, {0x10u, 0u}, {0x30u, 0u}, {0x40u, 0u}, {0x44u, 0u}, {0x48u, 0u}};

}

const char* Omap3530CmPer::RegisterName(uint32_t off) const {
    switch (off) {
    case 0x00: return "CM_FCLKEN_PER";
    case 0x10: return "CM_ICLKEN_PER";
    case 0x20: return "CM_IDLEST_PER";
    case 0x30: return "CM_AUTOIDLE_PER";
    case 0x40: return "CM_CLKSEL_PER";
    case 0x44: return "CM_SLEEPDEP_PER";
    case 0x48: return "CM_CLKSTCTRL_PER";
    case 0x4C: return "CM_CLKSTST_PER";
    }
    return nullptr;
}

uint32_t Omap3530CmPer::McBspBit(uint32_t port) const { return 1u << (port - 2u); }

uint32_t Omap3530CmPer::McBspMask() const { return kMcBspBits; }

bool Omap3530CmPer::LoadResetLocked() {
    StoreResetValuesLocked(kWarmReset);
    return false;
}

REGISTER_SERVICE(Omap3530CmPer);

}
