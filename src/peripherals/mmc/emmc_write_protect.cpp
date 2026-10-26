#include "emmc_card_base.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

namespace cerfcore {

using namespace cerf_mmc;

uint8_t EmmcCardBase::PartExtCsd(uint32_t offset) const {
    for (const EmmcExtCsdByte& p : ExtCsdProperties()) if (p.offset == offset) return p.value;
    return 0u;
}

// JEDEC JESD84-A43 printed pp. 84-85, ERASE_GRP_SIZE and WP_GRP_SIZE; section 8.4 HC_WP_GRP_SIZE
// with ERASE_GROUP_DEF enabled: 512KB * HC_ERASE_GRP_SIZE * HC_WP_GRP_SIZE
uint32_t EmmcCardBase::WpGroupSectors() const {
    const uint32_t hc_wp = PartExtCsd(kExtCsdHcWpGrpSize);
    if (erase_group_def_ != 0u && hc_wp != 0u)
        return kSectorsPer512K * PartExtCsd(kExtCsdHcEraseGrpSize) * hc_wp;
    const EmmcCsdFields csd = Csd();
    return (uint32_t(csd.wp_grp_size) + 1u) *
           (uint32_t(csd.erase_grp_size) + 1u) *
           (uint32_t(csd.erase_grp_mult) + 1u);
}

uint32_t EmmcCardBase::WpGroupCount() const {
    const uint32_t group = WpGroupSectors();
    return (SectorCount() + group - 1u) / group;
}

void EmmcCardBase::SetWriteProtect(uint32_t sector) {
    const uint32_t group = WpGroupSectors();
    if (sector % group != 0u) {
        emu_.Get<Fatal>().Die(
            "eMMC card in slot %u: SET_WRITE_PROT addresses sector %u, which is "
            "not on a %u-sector write protect group boundary", SlotIndex(),
            sector, group);
    }
    if ((user_wp_ & kUserWpPwrWpEn) == 0u) {
        emu_.Get<Fatal>().Die(
            "eMMC card in slot %u: SET_WRITE_PROT with USER_WP 0x%02X applies "
            "temporary write protection, which is not modeled", SlotIndex(),
            static_cast<unsigned>(user_wp_));
    }
    power_on_wp_[sector / group] = 1u;
}

void EmmcCardBase::RequireWritable(uint32_t sector) const {
    if (sector >= SectorCount()) {
        emu_.Get<Fatal>().Die(
            "eMMC card in slot %u: a multiple block write runs past the last "
            "sector into sector %u, and the error reported to the stop command "
            "is not modeled", SlotIndex(), sector);
    }
    if (power_on_wp_[sector / WpGroupSectors()] != 0u) {
        emu_.Get<Fatal>().Die(
            "eMMC card in slot %u: a write to sector %u lands inside a power-on "
            "write protected group, and the write protect violation is not "
            "modeled", SlotIndex(), sector);
    }
}

void EmmcCardBase::ApplyUserWp(uint32_t access, uint32_t value) {
    uint32_t next = user_wp_;
    switch (access) {
    case kSwitchSetBits:   next |= value;  break;
    case kSwitchClearBits: next &= ~value; break;
    case kSwitchWriteByte: next = value;   break;
    default:
        emu_.Get<Fatal>().Die(
            "eMMC card in slot %u: SWITCH access mode %u on USER_WP is not "
            "modeled", SlotIndex(), access);
    }
    if ((next & ~kUserWpPwrWpEn) != 0u) {
        emu_.Get<Fatal>().Die(
            "eMMC card in slot %u: SWITCH leaves USER_WP at 0x%02X, which sets a "
            "bit that is not modeled", SlotIndex(), next);
    }
    user_wp_ = static_cast<uint8_t>(next);
}

}
