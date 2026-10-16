#include "mc13783_adc.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/toshiba_gigabeat_s/toshiba_gigabeat_s_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

REGISTER_SERVICE(Mc13783Adc);

namespace {

/* MC13783 UG Table 13-44 (page 13-39): register 43 ADINC1 16, ADINC2 17, WCOMP 18, bits 21-22 read only;
   Table 13-45 (page 13-40): register 44 ADEN 0, ADA1 7:5, ADA2 10:8, ATO 18:11, ATOX 19, ASC 20,
   ADCBIS1 23; Table 13-47 (page 13-42): register 46 WLOW 14:9, bits 15-22 read only. */
constexpr uint32_t kAdinc1 = 1u << 16, kAdinc2 = 1u << 17, kWcomp = 1u << 18;
constexpr uint32_t kAden = 1u << 0, kAsc = 1u << 20, kAdcBis = 1u << 23;
constexpr uint32_t kAda1Shift = 5u, kAda2Shift = 8u, kWlowShift = 9u;
constexpr uint32_t kCtrlMask[3] = {0x1FFFFFu, 0x7FFFFFu, 0x7FFFu};

/* MC13783 UG Table 13-44 (page 13-39): register 43 CHRGRAWDIV bit 15 default 1; Table 13-47 (page
   13-42): register 46 ICID 8:6 default 010. */
constexpr uint32_t kCtrlReset[3] = {1u << 15, 0u, 1u << 7};

constexpr const char* kCtrlKeys[2] = {"adc_req0_ctrl", "adc_req1_ctrl"};

/* MC13783 UG Table 13-1 (page 13-4): ADCDONEI 0, ADCBISDONEI 1, WLOWI 4. */
constexpr uint32_t kAdcDonei = 1u << 0, kAdcBisDonei = 1u << 1, kWlowi = 1u << 4;

/* MC13783 UG Table 9-22 (page 9-18): ADCSEL 01 "Primary SPI can queue two ADC conversion requests",
   10 "Primary SPI has no ADC access". */
constexpr uint32_t kAdcselTwoRequests = 1u, kAdcselNoAccess = 2u;

}

bool Mc13783Adc::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::ToshibaGigabeatS;
}

void Mc13783Adc::OnReady() { Reset(); }

void Mc13783Adc::Reset() {
    for (Request& r : req_) {
        for (uint32_t i = 0; i < 3u; ++i) r.ctrl[i] = kCtrlReset[i];
    }
}

int Mc13783Adc::ControlIndex(uint32_t addr) {
    switch (addr) {
    case kRegAdc0:      return 0;
    case kRegAdc0 + 1u: return 1;
    case kRegAdc0 + 3u: return 2;
    default:            return -1;
    }
}

/* MC13783 UG section 9.7 (page 9-19): "By setting this bit to a 1, the control bits which follow are destined
   for the ADC BIS"; "In case of dual SPI control, so ADCSEL[1:0]=00, the ADCBISn bit will have no function". */
uint32_t Mc13783Adc::TargetRequest(uint32_t data, uint32_t adcsel) {
    return adcsel == kAdcselTwoRequests && (data & kAdcBis) != 0u ? 1u : 0u;
}

uint32_t Mc13783Adc::Read(uint32_t addr, uint32_t adcsel) {
    if (adcsel == kAdcselNoAccess) return 0u;
    const int idx = ControlIndex(addr);
    if (idx >= 0) return req_[0].ctrl[idx];
    return ReadResult(addr == kRegAdc4 ? 1u : 0u);
}

/* MC13783 UG section 9.3.2 (page 9-5): "Any SPI read of the A/D result register will return the values of
   the conversions indexed by ADA1[2:0] and ADA2[2:0]"; with ADINC1 / ADINC2 set the ADA bits "will
   automatically increment during subsequent readings of the A/D result register" (page 9-6). */
uint32_t Mc13783Adc::ReadResult(uint32_t req) {
    Request&       r    = req_[req];
    uint32_t       ada1 = (r.ctrl[1] >> kAda1Shift) & 7u;
    uint32_t       ada2 = (r.ctrl[1] >> kAda2Shift) & 7u;
    if ((r.ctrl[0] & kAdinc1) != 0u) ada1 = (ada1 + 1u) & 7u;
    if ((r.ctrl[0] & kAdinc2) != 0u) ada2 = (ada2 + 1u) & 7u;
    r.ctrl[1] = (r.ctrl[1] & ~((7u << kAda1Shift) | (7u << kAda2Shift))) | (ada1 << kAda1Shift) |
                (ada2 << kAda2Shift);
    return 0u;
}

/* MC13783 UG section 9.3.1 (page 9-5): "ADC completely ignores either ADTRIG or ASC pulses while ADEN is
   low". */
void Mc13783Adc::Write(uint32_t addr, uint32_t data, uint32_t adcsel) {
    if (adcsel == kAdcselNoAccess) return;
    const int idx = ControlIndex(addr);
    if (idx < 0) return;
    const uint32_t req = TargetRequest(data, adcsel);
    Request&       r   = req_[req];
    r.ctrl[idx] = data & kCtrlMask[idx];
    if (idx != 1 || (data & kAsc) == 0u || (data & kAden) == 0u) return;
    Complete(req);
}

/* MC13783 UG section 9.3.1 (page 9-4): "ASC will reset to zero and ADCDONEI will be set when complete";
   page 9-3: with WCOMP "If one of the conversions is greater than WHIGH or smaller than WLOW the interrupt
   pin will be asserted at the end of the conversions"; section 9.7 (page 9-20) WCOMP on non-BIS requests. */
void Mc13783Adc::Complete(uint32_t req) {
    Request& r = req_[req];
    r.ctrl[1] &= ~kAsc;
    uint32_t bits = req == 0u ? kAdcDonei : kAdcBisDonei;
    if (req == 0u && (r.ctrl[0] & kWcomp) != 0u && ((r.ctrl[2] >> kWlowShift) & 0x3Fu) != 0u) bits |= kWlowi;
    raise_(bits);
}

void Mc13783Adc::Save(StateWriter& w) {
    static_assert(sizeof(kCtrlKeys) / sizeof(kCtrlKeys[0]) == kRequests, "one key per request");
    for (uint32_t i = 0; i < kRequests; ++i) w.WriteBytes(kCtrlKeys[i], req_[i].ctrl, sizeof(req_[i].ctrl));
}

void Mc13783Adc::Restore(StateReader& r) {
    for (uint32_t i = 0; i < kRequests; ++i) r.ReadBytes(kCtrlKeys[i], req_[i].ctrl, sizeof(req_[i].ctrl));
}

}
