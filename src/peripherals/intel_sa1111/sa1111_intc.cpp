#include "sa1111_intc.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "../../boards/hp_jornada_700/hp_jornada_700_family_id.h"
#include "../../socs/sa11xx/sa11xx_gpio.h"
#include <cerfcore/state/state_stream.h>
#include "sa1111_sbi.h"
#include "sa1111_system_controller.h"

namespace cerfcore {

namespace {

/* SA-1111 Developer's Manual Table 3-3 note 1: "All reserved bits are read back as zero."
   Table 11-1: sources 27:31 and 55:63 reserved; §11.5.5 INTTSTSEL bits 1:0. */
constexpr uint32_t kSources[2] = {0x07FFFFFFu, 0x007FFFFFu};
constexpr uint32_t kTstselBits = 0x3u;

/* §11.5.5 INTTSTSEL: bit 0 "A one sets the source as the test register"; bit 1 "A one sets the
   mode as wake-up and a zero sets the mode as normal". */
constexpr uint32_t kTstselTest = 1u << 0;
constexpr uint32_t kTstselWake = 1u << 1;

}

bool Sa1111Intc::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
}

void Sa1111Intc::OnUnitReady() {
    sbi_ = &emu_.Get<Sa1111Sbi>();
    emu_.Get<Sa1111SystemController>().RegisterClockListener([this] { OnModeInputChange(); });
}

/* SA-1111 Developer's Manual §2.3: "Unless indicated otherwise, all register bits are set to
   zero during reset."; Figure 11-1: BnRES resets IntLatched(n). */
void Sa1111Intc::OnChipReset(bool held) {
    if (!held) return;
    std::lock_guard<std::mutex> lk(mtx_);
    for (Bank& b : bank_) {
        b.test = b.enable = b.polarity = b.status = b.wake_en = b.wake_pol = 0u;
    }
    tstsel_ = 0u;
    sleep_  = false;
    for (Bank& b : bank_) b.clock = LatchClock(b);
    DriveCascadeOutput(false);
}

void Sa1111Intc::RegisterSampler(std::function<void()> sample) {
    std::lock_guard<std::mutex> lk(mtx_);
    samplers_.push_back(std::move(sample));
}

void Sa1111Intc::SampleSources() {
    for (auto& fn : samplers_) fn();
}

/* §2.3: "When nRESET is asserted, all on-chip activity halts". */
void Sa1111Intc::SetSourceLevel(uint8_t source, bool level) {
    std::lock_guard<std::mutex> lk(mtx_);
    Bank&          b   = bank_[source >= 32u ? 1 : 0];
    const uint32_t bit = 1u << (source & 31u);
    b.raw = level ? (b.raw | bit) : (b.raw & ~bit);
    if (ChipHeld()) {
        b.clock = LatchClock(b);
        return;
    }
    Clock(b);
    DriveCascadeOutput(false);
}

void Sa1111Intc::StrobeSource(uint8_t source) {
    std::lock_guard<std::mutex> lk(mtx_);
    Bank&          b   = bank_[source >= 32u ? 1 : 0];
    const uint32_t bit = 1u << (source & 31u);
    if (ChipHeld() || (b.raw & bit) != 0u) return;
    b.raw |= bit;
    Clock(b);
    b.raw &= ~bit;
    Clock(b);
    DriveCascadeOutput(false);
}

/* §11.3.2 (printed 11-5): "Wake-up interrupts are only active when the Sleep state has been
   initiated by software"; §11.5.5 INTTSTSEL bit 1 selects the wake-up mode. */
bool Sa1111Intc::WakeMode() const {
    return sleep_ || (tstsel_ & kTstselWake) != 0u;
}

/* Figure 11-1 (printed 11-2): IntLatched(n) is clocked by (source XOR IntPol) AND ~Sleep AND IntEn,
   OR WakeupEN AND Sleep AND (source XOR WakeupPol), OR Set(n); TestSelect picks TestIn(n). */
uint32_t Sa1111Intc::LatchClock(const Bank& b) const {
    const uint32_t source = (tstsel_ & kTstselTest) != 0u ? b.test : b.raw;
    if (WakeMode()) return (source ^ b.wake_pol) & b.wake_en;
    return (source ^ b.polarity) & b.enable;
}

void Sa1111Intc::Clock(Bank& b) {
    const uint32_t clock = LatchClock(b);
    b.status |= clock & ~b.clock;
    b.clock   = clock;
}

void Sa1111Intc::ClockBoth() {
    Clock(bank_[0]);
    Clock(bank_[1]);
}

uint32_t Sa1111Intc::ClockedSources1() {
    std::lock_guard<std::mutex> lk(mtx_);
    if ((tstsel_ & kTstselTest) != 0u) return 0u;
    return WakeMode() ? bank_[1].wake_en : bank_[1].enable;
}

uint32_t Sa1111Intc::ClockPolarity1() {
    std::lock_guard<std::mutex> lk(mtx_);
    return WakeMode() ? bank_[1].wake_pol : bank_[1].polarity;
}

void Sa1111Intc::OnModeInputChange() {
    SampleSources();
    {
        std::lock_guard<std::mutex> lk(mtx_);
        const bool sleep = sbi_->SleepRequested();
        if (sleep == sleep_) return;
        sleep_ = sleep;
        if (!ChipHeld()) {
            ClockBoth();
            DriveCascadeOutput(false);
        }
    }
    SampleSources();
}

/* Offsets within the 0x40001600 block, Developer's Manual Table 11-2. */
uint32_t Sa1111Intc::UnitReadWord(uint32_t addr) {
    SampleSources();
    std::lock_guard<std::mutex> lk(mtx_);
    switch (addr - MmioBase()) {
        /* §11.5.2 (printed 11-6): INTTEST "On read, only bit 0 is significant indicating the
           logical OR of all the interrupt bits." */
        case 0x00: return bank_[0].test != 0u ? 1u : 0u;
        case 0x04: return bank_[1].test != 0u ? 1u : 0u;
        case 0x08: return bank_[0].enable;
        case 0x0C: return bank_[1].enable;
        case 0x10: return bank_[0].polarity;
        case 0x14: return bank_[1].polarity;
        case 0x18: return tstsel_;
        case 0x1C: return bank_[0].status;
        case 0x20: return bank_[1].status;
        /* §11.5.7 (printed 11-7): INTSET "When read, this register returns the value of the
           interrupt before it has been synchronized"; Figure 11-1 (printed 11-2): IntLatched(n). */
        case 0x24: return bank_[0].status;
        case 0x28: return bank_[1].status;
        case 0x2C: return bank_[0].wake_en;
        case 0x30: return bank_[1].wake_en;
        case 0x34: return bank_[0].wake_pol;
        case 0x38: return bank_[1].wake_pol;
    }
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

bool Sa1111Intc::WriteClockInput(uint32_t off, uint32_t value) {
    std::lock_guard<std::mutex> lk(mtx_);
    switch (off) {
        case 0x00: bank_[0].test     = value & kSources[0]; break;
        case 0x04: bank_[1].test     = value & kSources[1]; break;
        case 0x08: bank_[0].enable   = value & kSources[0]; break;
        case 0x0C: bank_[1].enable   = value & kSources[1]; break;
        case 0x10: bank_[0].polarity = value & kSources[0]; break;
        case 0x14: bank_[1].polarity = value & kSources[1]; break;
        case 0x18: tstsel_           = value & kTstselBits; break;
        case 0x2C: bank_[0].wake_en  = value & kSources[0]; break;
        case 0x30: bank_[1].wake_en  = value & kSources[1]; break;
        case 0x34: bank_[0].wake_pol = value & kSources[0]; break;
        case 0x38: bank_[1].wake_pol = value & kSources[1]; break;
        default:   return false;
    }
    ClockBoth();
    DriveCascadeOutput(false);
    return true;
}

void Sa1111Intc::UnitWriteWord(uint32_t addr, uint32_t value) {
    SampleSources();
    const uint32_t off = addr - MmioBase();
    if (WriteClockInput(off, value)) {
        SampleSources();
        return;
    }
    std::lock_guard<std::mutex> lk(mtx_);
    switch (off) {
        /* §11.5.6: "Writing a one clears the interrupt". */
        case 0x1C: bank_[0].status &= ~value; DriveCascadeOutput(true); return;
        case 0x20: bank_[1].status &= ~value; DriveCascadeOutput(true); return;
        /* §11.5.7 INTSET: "Writing a one sets the interrupt value"; Figure 11-1: Set(n) is an
           input of the OR that clocks IntLatched(n). */
        case 0x24:
            bank_[0].status |= value & kSources[0] & ~bank_[0].clock;
            DriveCascadeOutput(false);
            return;
        case 0x28:
            bank_[1].status |= value & kSources[1] & ~bank_[1].clock;
            DriveCascadeOutput(false);
            return;
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

/* Figure 11-2 (printed 11-2): INT is the OR of IntLatched(0..63) AND ~AnyClear; "any interrupt
   status clear forces INT to be low as long as AnyClear is asserted". */
void Sa1111Intc::DriveCascadeOutput(bool pulse_low_first) {
    auto& gpio = emu_.Get<Sa11xxGpio>();
    if (pulse_low_first) gpio.DriveInputPin(1, false);
    gpio.DriveInputPin(1, (bank_[0].status | bank_[1].status) != 0u);
}

void Sa1111Intc::SaveState(StateWriter& w) {
    SampleSources();
    std::lock_guard<std::mutex> lk(mtx_);
    w.Write("raw0", bank_[0].raw);                w.Write("raw1", bank_[1].raw);
    w.Write("latch_clock0", bank_[0].clock);      w.Write("latch_clock1", bank_[1].clock);
    w.Write("inttest0", bank_[0].test);           w.Write("inttest1", bank_[1].test);
    w.Write("enable0", bank_[0].enable);          w.Write("enable1", bank_[1].enable);
    w.Write("polarity0", bank_[0].polarity);      w.Write("polarity1", bank_[1].polarity);
    w.Write("tstsel", tstsel_);
    w.Write("status0", bank_[0].status);          w.Write("status1", bank_[1].status);
    w.Write("wake_en0", bank_[0].wake_en);        w.Write("wake_en1", bank_[1].wake_en);
    w.Write("wake_pol0", bank_[0].wake_pol);      w.Write("wake_pol1", bank_[1].wake_pol);
}

void Sa1111Intc::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(mtx_);
    r.Read("raw0", bank_[0].raw);                 r.Read("raw1", bank_[1].raw);
    r.Read("latch_clock0", bank_[0].clock);       r.Read("latch_clock1", bank_[1].clock);
    r.Read("inttest0", bank_[0].test);            r.Read("inttest1", bank_[1].test);
    r.Read("enable0", bank_[0].enable);           r.Read("enable1", bank_[1].enable);
    r.Read("polarity0", bank_[0].polarity);       r.Read("polarity1", bank_[1].polarity);
    r.Read("tstsel", tstsel_);
    r.Read("status0", bank_[0].status);           r.Read("status1", bank_[1].status);
    r.Read("wake_en0", bank_[0].wake_en);         r.Read("wake_en1", bank_[1].wake_en);
    r.Read("wake_pol0", bank_[0].wake_pol);       r.Read("wake_pol1", bank_[1].wake_pol);
}

void Sa1111Intc::PostRestore() {
    std::lock_guard<std::mutex> lk(mtx_);
    sleep_ = sbi_->SleepRequested();
    DriveCascadeOutput(false);
}

REGISTER_SERVICE(Sa1111Intc);

}
