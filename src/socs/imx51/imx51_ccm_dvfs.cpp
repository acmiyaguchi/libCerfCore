#include "imx51_ccm_dvfs.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>
#include "imx51_id.h"

namespace cerfcore {

namespace {

/* MCIMX51RM Figure 7-21 / Table 7-24 CDCR: sw_periph_clk_div_req_status [7] w1c,
   sw_periph_clk_div_req [6], software_DVFS_en [5], [4:3] read 0, arm_freq_shift_divider [2],
   periph_clk_DVFS_podf [1:0], reset 0x00000001. */
constexpr uint32_t kCdcrStatus      = 1u << 7;
constexpr uint32_t kCdcrSwReq       = 1u << 6;
constexpr uint32_t kCdcrSwEn        = 1u << 5;
constexpr uint32_t kCdcrShiftDiv    = 1u << 2;
constexpr uint32_t kCdcrPodf        = 0x3u;
constexpr uint32_t kCdcrWritable    = 0x67u;
constexpr uint32_t kCdcrReset       = 0x00000001u;

/* MCIMX51RM Figure 7-20 CDHIPR arm_podf_busy [16]. */
constexpr uint32_t kCdhiprArmPodfBusy = 1u << 16;

}

REGISTER_SERVICE(Imx51CcmDvfs);

bool Imx51CcmDvfs::ShouldRegister() {
    return emu_.Get<BoardContext>().GetSocId() == SocId::Imx51;
}

void Imx51CcmDvfs::Reset() {
    cdcr_        = kCdcrReset;
    arm_podf_    = 0u;
    arm_held_    = false;
    bus_divided_ = false;
    bus_div_     = 1u;
}

/* MCIMX51RM §7.4.7.2 (page 7-103): CDCR[0-1] "will not be loaded immediately to the actual divider,
   but will wait until peripheral DVFS procedure will commence"; page 7-104: with
   sw_periph_clk_div_req "the GPC control will be ignored". */
void Imx51CcmDvfs::WriteCdcr(uint32_t value) {
    const uint32_t status = (value & kCdcrStatus) != 0u ? 0u : (cdcr_ & kCdcrStatus);
    cdcr_ = (value & kCdcrWritable) | status;
    UpdateBusDivider();
}

void Imx51CcmDvfs::UpdateBusDivider() {
    const bool software = (cdcr_ & kCdcrSwEn) != 0u;
    const bool request  = software && (cdcr_ & kCdcrSwReq) != 0u;
    if (request == bus_divided_) return;
    bus_divided_ = request;
    const uint32_t podf = cdcr_ & kCdcrPodf;
    bus_div_ = request && podf != 0u ? podf + 1u : 1u;
    if (software) cdcr_ |= kCdcrStatus;
    rates_();
}

uint32_t Imx51CcmDvfs::Cdhipr() const {
    return arm_held_ ? kCdhiprArmPodfBusy : 0u;
}

/* MCIMX51RM Table 7-10: "if arm_freq_shift_divider is set to '1' then any new write to arm_podf will
   be held until arm_clk_switch_req signal is asserted"; Table 7-24 arm_freq_shift_divider 0: "any
   new writes to arm_podf will immediately change the frequency". */
bool Imx51CcmDvfs::WriteArmPodf(uint32_t podf) {
    if ((cdcr_ & kCdcrShiftDiv) != 0u) {
        arm_held_ = arm_held_ || podf != arm_podf_;
        return false;
    }
    const bool changed = podf != arm_podf_;
    arm_podf_ = podf;
    arm_held_ = false;
    return changed;
}

void Imx51CcmDvfs::SaveState(StateWriter& w) {
    w.Write("cdcr", cdcr_);
    w.Write("arm_podf", arm_podf_);
    w.Write<uint8_t>("arm_held", arm_held_ ? 1u : 0u);
    w.Write<uint8_t>("bus_divided", bus_divided_ ? 1u : 0u);
    w.Write("bus_div", bus_div_);
}

void Imx51CcmDvfs::RestoreState(StateReader& r) {
    uint8_t held = 0, divided = 0;
    r.Read("cdcr", cdcr_);
    r.Read("arm_podf", arm_podf_);
    r.Read("arm_held", held);
    r.Read("bus_divided", divided);
    r.Read("bus_div", bus_div_);
    arm_held_    = held != 0u;
    bus_divided_ = divided != 0u;
}

}
