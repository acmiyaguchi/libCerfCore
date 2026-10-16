#include "imx51_ccm_status.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>
#include "../irq_controller.h"
#include "imx51_clock_input.h"
#include "imx51_gpio1.h"
#include "imx51_id.h"

namespace cerfcore {

namespace {

/* MCIMX51RM Table 7-6 CCR. */
constexpr uint32_t kCcrCoscEn  = 1u << 12;
constexpr uint32_t kCcrCamp2En = 1u << 10;
constexpr uint32_t kCcrCamp1En = 1u << 9;
constexpr uint32_t kCcrFpmEn   = 1u << 8;

/* MCIMX51RM Table 7-26 CLPCR. */
constexpr uint32_t kClpcrCoscPwrdown = 1u << 11;
constexpr uint32_t kClpcrDisRefOsc   = 1u << 7;

/* MCIMX51RM Figure 7-5 CSR, Figure 7-24 CISR, both in cosc / CAMP2 / CAMP1 / FPM order. */
constexpr uint32_t kCsrLvsValue = 1u << 4;
constexpr uint32_t kCsrRefEnB   = 1u << 0;
constexpr uint32_t kCsrReady[]  = {1u << 5, 1u << 3, 1u << 2, 1u << 1};
constexpr uint32_t kCisrReady[] = {1u << 6, 1u << 5, 1u << 4, 1u << 3};
constexpr uint32_t kCisrInt1    = 0x07FF0000u;
constexpr uint32_t kCisrInt2    = 0x0000FFFFu;

/* MCIMX51RM Figure 7-25 CIMR: bits 31-27 read 1, every bit resets to 1. */
constexpr uint32_t kCimrReserved = 0xF8000000u;
constexpr uint32_t kCimrReset    = 0xFFFFFFFFu;

/* MCIMX51RM Figure 7-19 / Table 7-22 CWDR: gpio1_N_dir [21:16], gpio1_N_icr [11:0] for N = 4..9;
   icr 00 low level, 01 high level, 10 rise edge, 11 fall edge. Table 7-27: gpio1_4..9_wakeup_det
   are CISR bits 10..15. */
constexpr uint32_t kCwdrWritable  = 0x003F0FFFu;
constexpr uint32_t kCwdrDirShift  = 16;
constexpr uint32_t kWakePinFirst  = 4u;
constexpr uint32_t kWakePins      = 6u;
constexpr uint32_t kCisrWakeShift = 10;
constexpr uint32_t kIcrLow        = 0u;
constexpr uint32_t kIcrHigh       = 1u;
constexpr uint32_t kIcrRise       = 2u;
constexpr uint32_t kIcrFall       = 3u;

/* MCIMX51RM Table 3-2: IRQ 71 "CCM, Interrupt Request 1", IRQ 72 "CCM, Interrupt Request 2". */
constexpr int kIrqInt1 = 71;
constexpr int kIrqInt2 = 72;

}

REGISTER_SERVICE(Imx51CcmStatus);

bool Imx51CcmStatus::ShouldRegister() {
    return emu_.Get<BoardContext>().GetSocId() == SocId::Imx51;
}

void Imx51CcmStatus::OnReady() {
    gpio1_  = &emu_.Get<Imx51Gpio1>();
    osc_hz_ = emu_.Get<Imx51ClockInput>().OscHz();
    gpio1_->RegisterInputListener([this](uint32_t pin, bool level) { OnGpioInput(pin, level); });
}

void Imx51CcmStatus::Reset(uint32_t ccr, uint32_t clpcr) {
    source_on_.fill(false);
    {
        std::lock_guard<std::mutex> lk(mu_);
        cisr_   = 0u;
        cimr_   = kCimrReset;
        cwdr_   = 0u;
        levels_ = 0u;
        for (uint32_t n = 0; n < kWakePins; ++n) {
            const uint32_t pin = kWakePinFirst + n;
            if (gpio1_->PinPadLevel(pin)) levels_ |= 1u << pin;
        }
        DriveLocked();
    }
    OnControl(ccr, clpcr);
}

/* MCIMX51RM Table 7-8: cosc_ready also needs the oscillator "not powered down" (Table 7-26
   cosc_pwrdown). */
void Imx51CcmStatus::OnControl(uint32_t ccr, uint32_t clpcr) {
    const bool want[kSourceCount] = {
        (ccr & kCcrCoscEn) != 0u && (clpcr & kClpcrCoscPwrdown) == 0u,
        (ccr & kCcrCamp2En) != 0u,
        (ccr & kCcrCamp1En) != 0u,
        (ccr & kCcrFpmEn) != 0u,
    };
    for (uint32_t i = 0; i < kSourceCount; ++i) {
        if (want[i] == source_on_[i]) continue;
        source_on_[i] = want[i];
        if (want[i]) Raise(kCisrReady[i]);
    }
}

uint32_t Imx51CcmStatus::Csr(uint32_t clpcr) const {
    uint32_t v = kCsrLvsValue;
    if ((clpcr & kClpcrDisRefOsc) != 0u) v |= kCsrRefEnB;
    for (uint32_t i = 0; i < kSourceCount; ++i) {
        if (source_on_[i]) v |= kCsrReady[i];
    }
    return v;
}

uint32_t Imx51CcmStatus::Cisr() {
    std::lock_guard<std::mutex> lk(mu_);
    return cisr_;
}

uint32_t Imx51CcmStatus::Cimr() {
    std::lock_guard<std::mutex> lk(mu_);
    return cimr_;
}

uint32_t Imx51CcmStatus::Cwdr() {
    std::lock_guard<std::mutex> lk(mu_);
    return cwdr_;
}

void Imx51CcmStatus::WriteCisr(uint32_t value) {
    std::lock_guard<std::mutex> lk(mu_);
    cisr_ &= ~value;
    RaiseLevelsLocked();
    DriveLocked();
}

void Imx51CcmStatus::WriteCimr(uint32_t value) {
    std::lock_guard<std::mutex> lk(mu_);
    cimr_ = value | kCimrReserved;
    RaiseLevelsLocked();
    DriveLocked();
}

void Imx51CcmStatus::WriteCwdr(uint32_t value) {
    std::lock_guard<std::mutex> lk(mu_);
    cwdr_ = value & kCwdrWritable;
    RaiseLevelsLocked();
    DriveLocked();
}

void Imx51CcmStatus::Raise(uint32_t cisr_bits) {
    std::lock_guard<std::mutex> lk(mu_);
    RaiseLocked(cisr_bits);
    DriveLocked();
}

/* MCIMX51RM Table 7-27 bit 10: "Interrupt ipi_int_2 generation due to change of gpio1_4 input." */
void Imx51CcmStatus::OnGpioInput(uint32_t pin, bool level) {
    if (pin < kWakePinFirst || pin >= kWakePinFirst + kWakePins) return;
    std::lock_guard<std::mutex> lk(mu_);
    const uint32_t bit  = 1u << pin;
    const bool     prev = (levels_ & bit) != 0u;
    levels_ = level ? (levels_ | bit) : (levels_ & ~bit);
    const uint32_t n = pin - kWakePinFirst;
    if (((cwdr_ >> (kCwdrDirShift + n)) & 1u) == 0u) {
        const uint32_t icr = (cwdr_ >> (2u * n)) & 3u;
        if ((icr == kIcrRise && !prev && level) || (icr == kIcrFall && prev && !level)) {
            RaiseLocked(1u << (kCisrWakeShift + n));
        }
    }
    RaiseLevelsLocked();
    DriveLocked();
}

void Imx51CcmStatus::RaiseLevelsLocked() {
    for (uint32_t n = 0; n < kWakePins; ++n) {
        const uint32_t pin = kWakePinFirst + n;
        if (((cwdr_ >> (kCwdrDirShift + n)) & 1u) != 0u || !gpio1_->PinDriven(pin)) continue;
        const uint32_t icr   = (cwdr_ >> (2u * n)) & 3u;
        const bool     level = ((levels_ >> pin) & 1u) != 0u;
        if ((icr == kIcrLow && !level) || (icr == kIcrHigh && level)) {
            RaiseLocked(1u << (kCisrWakeShift + n));
        }
    }
}

/* MCIMX51RM Table 7-6 CAMP2_EN: the CCM notifies "by the CAMP2_ready interrupt (if the interrupt is
   not masked) and by the CAMP2_ready status bit". */
void Imx51CcmStatus::RaiseLocked(uint32_t bits) {
    cisr_ |= bits & ~cimr_;
}

void Imx51CcmStatus::DriveLocked() {
    auto& irq = emu_.Get<IrqController>();
    if ((cisr_ & kCisrInt1) != 0u) irq.AssertIrq(kIrqInt1);
    else                           irq.DeAssertIrq(kIrqInt1);
    if ((cisr_ & kCisrInt2) != 0u) irq.AssertIrq(kIrqInt2);
    else                           irq.DeAssertIrq(kIrqInt2);
}

void Imx51CcmStatus::SaveState(StateWriter& w) {
    uint8_t on = 0;
    for (uint32_t i = 0; i < kSourceCount; ++i) {
        if (source_on_[i]) on = static_cast<uint8_t>(on | (1u << i));
    }
    w.Write("source_on", on);
    std::lock_guard<std::mutex> lk(mu_);
    w.Write("cisr", cisr_);
    w.Write("cimr", cimr_);
    w.Write("cwdr", cwdr_);
    w.Write("wake_levels", levels_);
}

void Imx51CcmStatus::RestoreState(StateReader& r) {
    uint8_t on = 0;
    r.Read("source_on", on);
    for (uint32_t i = 0; i < kSourceCount; ++i) source_on_[i] = ((on >> i) & 1u) != 0u;
    std::lock_guard<std::mutex> lk(mu_);
    r.Read("cisr", cisr_);
    r.Read("cimr", cimr_);
    r.Read("cwdr", cwdr_);
    r.Read("wake_levels", levels_);
}

void Imx51CcmStatus::PostRestore() {
    std::lock_guard<std::mutex> lk(mu_);
    DriveLocked();
}

}
