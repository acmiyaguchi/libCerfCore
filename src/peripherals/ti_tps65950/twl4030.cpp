#include "twl4030.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/ti_omap35x_evm/ti_omap35x_evm_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "tps65950_audio_downlink.h"
#include "tps65950_audio_interface.h"
#include "tps65950_audio_uplink.h"
#include "tps65950_board.h"
#include "tps65950_interrupts.h"
#include "tps65950_power_master.h"
#include "tps65950_reset_image.h"
#include "tps65950_rtc.h"

#include <iterator>

namespace {
constexpr uint8_t kTwlAddr_USB         = 0x48;
constexpr uint8_t kTwlAddr_AUDIO_INTBR = 0x49;
constexpr uint8_t kTwlAddr_BCI_MADC    = 0x4A;
constexpr uint8_t kTwlAddr_PWR_RTC     = 0x4B;

constexpr uint8_t kCodecMode = 0x01u;
constexpr uint8_t kAudioIf   = 0x0Eu;
constexpr uint8_t kApllCtl   = 0x3Au;

constexpr uint8_t kSecuredRegU   = 0x13u;
constexpr uint8_t kSecondsReg    = 0x1Cu;
constexpr uint8_t kWeeksReg      = 0x22u;
constexpr uint8_t kRtcCtrlReg    = 0x29u;
constexpr uint8_t kRtcCompLsb    = 0x2Cu;
constexpr uint8_t kRtcCompMsb    = 0x2Du;
constexpr uint8_t kDcdcGlobalCfg = 0x61u;
constexpr uint8_t kVaux2DevGrp   = 0x76u;
constexpr uint8_t kClkDegateBits = 0x03u;

bool InterfaceTiming(uint8_t sub) { return sub == kCodecMode || sub == kAudioIf || sub == kApllCtl; }
bool UplinkPath(uint8_t sub) {
    return sub == kCodecMode || sub == 0x02u || (sub >= 0x05u && sub <= 0x08u) || (sub >= 0x0Au && sub <= 0x0Du) ||
           sub == 0x2Bu || sub == 0x48u;
}
bool DownlinkPath(uint8_t sub) {
    return sub == kCodecMode || sub == 0x02u || sub == 0x09u || (sub >= 0x10u && sub <= 0x13u) || sub == 0x17u ||
           (sub >= 0x19u && sub <= 0x1Du) || (sub >= 0x21u && sub <= 0x2Au) || sub == 0x2Eu || sub == 0x2Fu ||
           sub == 0x3Eu || sub == 0x43u || sub == 0x44u;
}
bool OwnedPower(uint8_t sub) { return Tps65950Rtc::Owns(sub) || Tps65950Interrupts::OwnsPower(sub); }

/* TPS65950 TRM SWCU050E Table 7-88 (page 492): SECURED_REG_A-U; Table 3-30 (page 137): the time registers, RTC_CTRL,
   RTC_COMP; Table 3-20 (page 133): DC/DC_GLOBAL_CFG [1:0]; Table 5-121 (page 304): VAUX2_DEV_GRP. */
uint8_t MsecureProtectedBits(uint8_t sub) {
    if (sub <= kSecuredRegU) return 0xFFu;
    if (sub >= kSecondsReg && sub <= kWeeksReg) return 0xFFu;
    if (sub == kRtcCtrlReg || sub == kRtcCompLsb || sub == kRtcCompMsb || sub == kVaux2DevGrp) return 0xFFu;
    if (sub == kDcdcGlobalCfg) return kClkDegateBits;
    return 0u;
}
}

bool Twl4030::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::TiOmap35xEvm;
}

void Twl4030::OnReady() {
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        LoadDigitalLocked();
        LoadPowerLocked(true);
    }
    emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] {
        if (!audio_reset_) return;
        audio_reset_ = false;
        AudioWillChange(true, true);
        AudioChanged(true, true, true);
    });
}

void Twl4030::LoadBankLocked(uint8_t slave_addr, const Tps65950RegValue* image, size_t count) {
    auto& bank = regs_[AddrIndex(slave_addr)];
    bank.fill(0u);
    for (size_t i = 0; i < count; ++i) bank[image[i].sub] = image[i].value;
}

/* TPS65950 TRM SWCU050E page 241: turning TRITON_RESET off and on "resets all the digital logic in the device, except
   for some in the PWR subchip"; Figure 5-5 (page 243): TRITON2_RESET_NA to the JTAG, INT, AUDIO, USB and AUX blocks. */
void Twl4030::LoadDigitalLocked() {
    LoadBankLocked(kTwlAddr_USB, tps65950::kUsbPowerOn, std::size(tps65950::kUsbPowerOn));
    LoadBankLocked(kTwlAddr_AUDIO_INTBR, tps65950::kAudioIntPowerOn, std::size(tps65950::kAudioIntPowerOn));
    LoadBankLocked(kTwlAddr_BCI_MADC, tps65950::kAuxPowerOn, std::size(tps65950::kAuxPowerOn));
}

void Twl4030::LoadPowerLocked(bool backup_domain) {
    auto& bank = regs_[AddrIndex(kTwlAddr_PWR_RTC)];
    for (size_t sub = 0; sub < bank.size(); ++sub) {
        if (backup_domain || !tps65950::InBackupDomain(static_cast<uint8_t>(sub))) bank[sub] = 0u;
    }
    for (const Tps65950RegValue& v : tps65950::kPowerRtcPowerOn) {
        if (OwnedPower(v.sub)) continue;
        if (backup_domain || !tps65950::InBackupDomain(v.sub)) bank[v.sub] = v.value;
    }
}

void Twl4030::LoadPowerOn(bool backup_domain) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    LoadDigitalLocked();
    LoadPowerLocked(backup_domain);
    audio_reset_ = true;
}

void Twl4030::ResetDigital() {
    std::lock_guard<std::mutex> lk(state_mutex_);
    LoadDigitalLocked();
    audio_reset_ = true;
}

void Twl4030::AudioWillChange(bool timing, bool uplink) {
    if (timing || uplink) emu_.Get<Tps65950AudioInterface>().OnAudioRegisterWillChange();
}

void Twl4030::AudioChanged(bool timing, bool uplink, bool downlink) {
    if (uplink) emu_.Get<Tps65950AudioUplink>().Refresh();
    if (downlink) emu_.Get<Tps65950AudioDownlink>().Refresh();
    if (timing) emu_.Get<Tps65950AudioInterface>().OnAudioRegisterWrite();
}

bool Twl4030::MatchesAddress(uint8_t slave_addr) const {
    return slave_addr == kTwlAddr_USB         ||
           slave_addr == kTwlAddr_AUDIO_INTBR ||
           slave_addr == kTwlAddr_BCI_MADC    ||
           slave_addr == kTwlAddr_PWR_RTC;
}

void Twl4030::TxnStart(uint8_t slave_addr) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    const int idx = AddrIndex(slave_addr);
    sub_addr_pending_[idx] = true;
}

void Twl4030::TxnWriteByte(uint8_t slave_addr, uint8_t byte) {
    const int idx = AddrIndex(slave_addr);
    uint8_t   sub = 0;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        if (sub_addr_pending_[idx]) {
            sub_addr_[idx] = byte;
            sub_addr_pending_[idx] = false;
            return;
        }
        sub = sub_addr_[idx];
        sub_addr_[idx] = (sub_addr_[idx] + 1u) & 0xFFu;
    }
    if (slave_addr == kTwlAddr_PWR_RTC && !ApplyMsecure(sub, byte)) return;
    if (slave_addr == kTwlAddr_PWR_RTC && emu_.Get<Tps65950PowerMaster>().ConsumeWrite(sub, byte)) return;
    if (slave_addr == kTwlAddr_PWR_RTC && Tps65950Rtc::Owns(sub)) {
        emu_.Get<Tps65950Rtc>().Write(sub, byte);
        return;
    }
    if (slave_addr == kTwlAddr_PWR_RTC && Tps65950Interrupts::OwnsPower(sub)) {
        emu_.Get<Tps65950Interrupts>().WritePower(sub, byte);
        return;
    }
    if (slave_addr == kTwlAddr_AUDIO_INTBR && Tps65950Interrupts::OwnsPih(sub)) {
        emu_.Get<Tps65950Interrupts>().WritePih(sub, byte);
        return;
    }
    const bool audio    = slave_addr == kTwlAddr_AUDIO_INTBR;
    const bool timing   = audio && InterfaceTiming(sub);
    const bool uplink   = audio && UplinkPath(sub);
    const bool downlink = audio && DownlinkPath(sub);
    AudioWillChange(timing, uplink);
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        regs_[idx][sub] = byte;
    }
    AudioChanged(timing, uplink, downlink);
}

/* TPS65950 TRM SWCU050E Table 3-29 (page 137): the secure mode is P(MSecure) = 0; Table 3-20 (page 133): the
   degate bits take a write only with "MSECURE pad = 1". */
bool Twl4030::ApplyMsecure(uint8_t sub, uint8_t& byte) {
    const uint8_t protect = MsecureProtectedBits(sub);
    if (protect == 0u || emu_.Get<Tps65950Board>().MsecureHigh()) return true;
    if (protect == 0xFFu) return false;
    std::lock_guard<std::mutex> lk(state_mutex_);
    const uint8_t now = regs_[AddrIndex(kTwlAddr_PWR_RTC)][sub];
    byte              = static_cast<uint8_t>((now & protect) | (byte & ~protect));
    return true;
}

uint8_t Twl4030::TxnReadByte(uint8_t slave_addr) {
    uint8_t sub = 0, v = 0;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        const int idx = AddrIndex(slave_addr);
        sub = sub_addr_[idx];
        v   = regs_[idx][sub];
        sub_addr_[idx] = (sub_addr_[idx] + 1u) & 0xFFu;
    }
    uint8_t data = 0;
    if (slave_addr == kTwlAddr_PWR_RTC && emu_.Get<Tps65950PowerMaster>().ConsumeRead(sub, data)) return data;
    if (slave_addr == kTwlAddr_PWR_RTC && Tps65950Rtc::Owns(sub)) return emu_.Get<Tps65950Rtc>().Read(sub);
    if (slave_addr == kTwlAddr_PWR_RTC && Tps65950Interrupts::OwnsPower(sub)) {
        return emu_.Get<Tps65950Interrupts>().ReadPower(sub);
    }
    if (slave_addr == kTwlAddr_AUDIO_INTBR && Tps65950Interrupts::OwnsPih(sub)) {
        return emu_.Get<Tps65950Interrupts>().ReadPih(sub);
    }
    return v;
}

uint8_t Twl4030::AudioReg(uint8_t sub_addr) const {
    std::lock_guard<std::mutex> lk(state_mutex_);
    return regs_[AddrIndex(kTwlAddr_AUDIO_INTBR)][sub_addr];
}

uint8_t Twl4030::PowerReg(uint8_t sub_addr) const {
    std::lock_guard<std::mutex> lk(state_mutex_);
    return regs_[AddrIndex(kTwlAddr_PWR_RTC)][sub_addr];
}

void Twl4030::SetPowerReg(uint8_t sub_addr, uint8_t value) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    regs_[AddrIndex(kTwlAddr_PWR_RTC)][sub_addr] = value;
}

void Twl4030::SaveState(StateWriter& w) {
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        for (const auto& bank : regs_) w.WriteBytes("twl_regs", bank.data(), bank.size());
        w.WriteBytes("twl_sub_addr", sub_addr_.data(), sub_addr_.size());
        for (bool pending : sub_addr_pending_) w.Write<uint8_t>("twl_sub_addr_pending", pending ? 1u : 0u);
    }
    emu_.Get<Tps65950AudioInterface>().Save(w);
    emu_.Get<Tps65950PowerMaster>().Save(w);
    emu_.Get<Tps65950Rtc>().Save(w);
    emu_.Get<Tps65950Interrupts>().Save(w);
}

void Twl4030::RestoreState(StateReader& r) {
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        for (auto& bank : regs_) r.ReadBytes("twl_regs", bank.data(), bank.size());
        r.ReadBytes("twl_sub_addr", sub_addr_.data(), sub_addr_.size());
        for (bool& pending : sub_addr_pending_) {
            uint8_t v = 0;
            r.Read("twl_sub_addr_pending", v);
            pending = v != 0u;
        }
    }
    AudioChanged(false, true, true);
    emu_.Get<Tps65950AudioInterface>().Restore(r);
    emu_.Get<Tps65950PowerMaster>().Restore(r);
    emu_.Get<Tps65950Rtc>().Restore(r);
    emu_.Get<Tps65950Interrupts>().Restore(r);
}

void Twl4030::PostRestore() {
    emu_.Get<Tps65950Rtc>().PostRestore();
    emu_.Get<Tps65950Interrupts>().PostRestore();
}

int Twl4030::AddrIndex(uint8_t slave_addr) {
    switch (slave_addr) {
    case kTwlAddr_USB:         return 0;
    case kTwlAddr_AUDIO_INTBR: return 1;
    case kTwlAddr_BCI_MADC:    return 2;
    case kTwlAddr_PWR_RTC:     return 3;
    }
    LOG(Caution, "Twl4030: AddrIndex called with unknown slave_addr "
            "0x%02X - bus dispatch should have rejected this\n",
            slave_addr);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

REGISTER_SERVICE(Twl4030);
