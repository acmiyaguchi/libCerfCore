#include "sa11xx_mcp.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "sa1110_id.h"
#include "sa1100_id.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "sa11xx_dma.h"
#include "sa11xx_external_clocks.h"
#include "sa11xx_intc.h"
#include "sa11xx_mcp_audio_stream.h"
#include "sa11xx_mcp_codec.h"
#include "sa11xx_mcp_frame_clock.h"
#include "sa11xx_mcp_regs.h"
#include "sa11xx_mcp_telecom_stream.h"
#include "sa11xx_ppc.h"

#include <algorithm>

namespace {

using namespace sa11xx_mcp;

/* SA-1110 §9.2.1.1 (printed 9-12): IP18 "Serial port 4a", "MCP service request". */
constexpr uint32_t kIntcBitMcp = 1u << 18;

}

bool Sa11xxMcp::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && (bd->GetSocId() == SocId::Sa1110 || bd->GetSocId() == SocId::Sa1100);
}

void Sa11xxMcp::OnReady() {
    clock_   = &emu_.Get<GuestCycleClock>();
    dma_     = &emu_.Get<Sa11xxDma>();
    intc_    = &emu_.Get<Sa11xxIntc>();
    frames_  = &emu_.Get<Sa11xxMcpFrameClock>();
    audio_   = &emu_.Get<Sa11xxMcpAudioStream>();
    telecom_ = &emu_.Get<Sa11xxMcpTelecomStream>();
    audio_->SetLineListener([this](uint64_t now) { RefreshLine(now); });
    telecom_->SetLineListener([this](uint64_t now) { RefreshLine(now); });
    line_ev_ = clock_->Add([this] {
        const uint64_t now = clock_->Cycles();
        SettleChannels(now);
        RefreshLine(now);
    });
    clock_->RegisterRateListener([this] {
        const uint64_t now = clock_->Cycles();
        SettleChannels(now);
        frames_->OnCpuRate(now);
        RefreshLine(now);
        dma_->OnPortChange();
    });
    emu_.Get<Sa11xxPpc>().RegisterMccr1Listener([this] { ControlChanged(); });
    emu_.Get<Sa11xxExternalClocks>().RegisterChangeListener([this] { ControlChanged(); });
    /* SA-1110 Developer's Manual §11.12.3.11 (printed 11-137): "the MCE bit is the only control
       bit that is reset to a known state"; MCSR reset row (printed 11-149): CRC 0, CWC 0. */
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        mccr0_    &= ~kMce;
        cmd_valid_ = false;
        cwc_       = false;
        crc_       = false;
    });
    emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] {
        RefreshLine(clock_->Cycles());
    });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void Sa11xxMcp::SettleChannels(uint64_t now) {
    audio_->Settle(now);
    telecom_->Settle(now);
}

void Sa11xxMcp::ResumeFrames(uint64_t now) {
    const uint64_t frozen = frames_->FrozenTick();
    if (!frames_->Resume(now)) return;
    audio_->Resume(frozen, frames_->RunBase());
    telecom_->Resume(frozen, frames_->RunBase());
}

void Sa11xxMcp::ControlChanged() {
    const uint64_t now   = clock_->Cycles();
    const uint32_t mccr1 = emu_.Get<Sa11xxPpc>().Mccr1();
    ServeCommand(now);
    SettleChannels(now);
    const bool was = frames_->Mce();
    const bool mce = (mccr0_ & kMce) != 0u;
    if (was && !mce) {
        audio_->Freeze();
        telecom_->Freeze();
        frames_->Freeze(now);
    }
    audio_->PrepareControl(now, mccr0_, mccr1);
    telecom_->PrepareControl(now, mccr0_, mccr1);
    frames_->SetControl(mccr0_, mccr1);
    if (!was && mce) {
        ResumeFrames(now);
    } else if (mce) {
        if (frames_->Enabled() || frames_->Held()) frames_->Retime(now);
        else                                        ResumeFrames(now);
    }
    audio_->FinishControl(now);
    telecom_->FinishControl(now);
    RefreshLine(now);
    ServeCommand(now);
    dma_->OnPortChange();
    for (auto& fn : control_listeners_) fn();
}

void Sa11xxMcp::RegisterControlListener(std::function<void()> fn) {
    control_listeners_.push_back(std::move(fn));
}

/* §11.12.6 (printed 11-145): "Each of these conditions signal an interrupt request to the interrupt
   controller"; §9.2.1.1 (printed 9-12): IP18 carries the MCP's 8 second-level sources. */
void Sa11xxMcp::RefreshLine(uint64_t now) {
    const bool level = audio_->LineLevel() || telecom_->LineLevel();
    intc_->SetSourceLevel(kIntcBitMcp, level ? kIntcBitMcp : 0u);
    uint64_t tick = kNever, at = 0;
    if (!level && audio_->NextRise(at)) tick = at;
    if (!level && telecom_->NextRise(at)) tick = std::min(tick, at);
    if (tick == kNever) {
        clock_->Disarm(line_ev_);
        return;
    }
    clock_->Arm(line_ev_, std::max(frames_->CycleOfSclk(tick), now));
}

/* §11.12.5.3 (printed 11-144): "the operation is performed every MCP data frame until a new value
   is written to the register"; §11.12.6.13 / §11.12.6.14 (printed 11-148): CWC / CRC are set
   after a sequence that starts with "writing to MCDR2". */
void Sa11xxMcp::ServeCommand(uint64_t now) {
    uint64_t t = 0;
    if (!cmd_valid_ || !frames_->SclkTick(now, t)) return;
    if (cmd_write_) {
        /* §11.12.1.4 (printed 11-131): the MCDR2 contents "are transferred to the correct fields
           within the serial shifter on the next rising edge of the SFRM signal". */
        if (!cmd_done_) {
            PerformWrite(now, (t / kFrameTicks + 1u) * kFrameTicks, cmd_reg_, cmd_value_);
            cwc_ = true;
        }
        mcdr2_ = (static_cast<uint32_t>(cmd_reg_) << kMcdr2AddrShift) | cmd_value_;
    } else {
        mcdr2_ = (static_cast<uint32_t>(cmd_reg_) << kMcdr2AddrShift) |
                 ReturnedRead(cmd_reg_, cmd_sent_);
        if (!cmd_done_) crc_ = true;
    }
    cmd_done_ = true;
}

void Sa11xxMcp::PerformWrite(uint64_t now, uint64_t carry, uint8_t reg, uint16_t value) {
    const bool looped = Looped();
    if (!looped) {
        if (auto* codec = emu_.TryGet<Sa11xxMcpCodec>()) codec->WriteReg(reg, value);
    }
    const bool audio   = audio_->CodecWrite(now, carry, reg, value, looped);
    const bool telecom = telecom_->CodecWrite(now, carry, reg, value, looped);
    if (audio || telecom) dma_->OnPortChange();
}

/* §11.12.3.10 (printed 11-137): with LBM=1 "the output of the serial shifter (MSB) is directly
   connected to the input of the serial shifter (LSB) internally". */
bool Sa11xxMcp::Looped() const {
    return (mccr0_ & kLbm) != 0u;
}

uint16_t Sa11xxMcp::ReturnedRead(uint8_t reg, uint16_t sent) {
    if (Looped()) return sent;
    auto* codec = emu_.TryGet<Sa11xxMcpCodec>();
    return codec ? codec->ReadReg(reg) : 0u;
}

/* §11.12.6.13 / §11.12.6.14: CWC and CRC are "automatically cleared when MCDR2 is read or
   written". */
void Sa11xxMcp::ClearCompletion() {
    cwc_ = false;
    crc_ = false;
}

uint32_t Sa11xxMcp::ReadWord(uint32_t addr) {
    const uint64_t now = clock_->Cycles();
    switch (addr - MmioBase()) {
        case 0x00: return mccr0_;
        case 0x08: {
            const uint32_t value = audio_->ReadData(now);
            dma_->OnPortChange();
            return value;
        }
        case 0x0C: {
            const uint32_t value = telecom_->ReadData(now);
            dma_->OnPortChange();
            return value;
        }
        case 0x10: {
            ServeCommand(now);
            const uint32_t value = mcdr2_;
            ClearCompletion();
            return value;
        }
        case 0x18:
            ServeCommand(now);
            return audio_->Status(now) | telecom_->Status(now) | (cwc_ ? kCwc : 0u) |
                   (crc_ ? kCrc : 0u);
    }
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

void Sa11xxMcp::WriteWord(uint32_t addr, uint32_t value) {
    const uint64_t now = clock_->Cycles();
    switch (addr - MmioBase()) {
        case 0x00:
            mccr0_ = value;
            ControlChanged();
            return;
        case 0x08:
            audio_->WriteData(now, value);
            dma_->OnPortChange();
            return;
        case 0x0C:
            telecom_->WriteData(now, value);
            dma_->OnPortChange();
            return;
        case 0x10: RouteCodecCommand(value); return;
        case 0x18:
            audio_->ClearStatus(now, value);
            telecom_->ClearStatus(now, value);
            return;
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

void Sa11xxMcp::RouteCodecCommand(uint32_t cmd) {
    ClearCompletion();
    const bool is_write = (cmd & kMcdr2Write) != 0u;
    cmd_valid_ = true;
    cmd_write_ = is_write;
    cmd_done_  = false;
    cmd_reg_   = static_cast<uint8_t>((cmd >> kMcdr2AddrShift) & 0xFu);
    cmd_value_ = is_write ? static_cast<uint16_t>(cmd & 0xFFFFu) : 0u;
    cmd_sent_  = static_cast<uint16_t>(cmd & 0xFFFFu);
    ServeCommand(clock_->Cycles());
}

void Sa11xxMcp::SaveState(StateWriter& w) {
    const uint64_t now = clock_->Cycles();
    w.Write("mccr0", mccr0_);
    w.Write<uint8_t>("cmd_valid", cmd_valid_ ? 1u : 0u);
    w.Write<uint8_t>("cmd_write", cmd_write_ ? 1u : 0u);
    w.Write<uint8_t>("cmd_done", cmd_done_ ? 1u : 0u);
    w.Write("cmd_reg", cmd_reg_);
    w.Write("cmd_write_value", cmd_value_);
    w.Write("cmd_word", cmd_sent_);
    w.Write<uint8_t>("cmd_cwc", cwc_ ? 1u : 0u);
    w.Write<uint8_t>("cmd_crc", crc_ ? 1u : 0u);
    w.Write("mcdr2", mcdr2_);
    frames_->Save(w, now);
    w.BeginFrame(0u);
    audio_->Save(w);
    w.EndFrame();
    w.BeginFrame(1u);
    telecom_->Save(w);
    w.EndFrame();
    if (auto* codec = emu_.TryGet<Sa11xxMcpCodec>()) codec->SaveState(w);
}

void Sa11xxMcp::RestoreState(StateReader& r) {
    uint8_t valid = 0, write = 0, done = 0, cwc = 0, crc = 0;
    r.Read("mccr0", mccr0_);
    r.Read("cmd_valid", valid);
    r.Read("cmd_write", write);
    r.Read("cmd_done", done);
    r.Read("cmd_reg", cmd_reg_);
    r.Read("cmd_write_value", cmd_value_);
    r.Read("cmd_word", cmd_sent_);
    r.Read("cmd_cwc", cwc);
    r.Read("cmd_crc", crc);
    r.Read("mcdr2", mcdr2_);
    cmd_valid_ = valid != 0u;
    cmd_write_ = write != 0u;
    cmd_done_  = done != 0u;
    cwc_       = cwc != 0u;
    crc_       = crc != 0u;
    frames_->Restore(r);
    r.EnterFrame();
    audio_->Restore(r);
    r.LeaveFrame();
    r.EnterFrame();
    telecom_->Restore(r);
    r.LeaveFrame();
    if (auto* codec = emu_.TryGet<Sa11xxMcpCodec>()) codec->RestoreState(r);
}

void Sa11xxMcp::PostRestore() {
    emu_.Get<Sa11xxExternalClocks>().Resync();
    if (auto* codec = emu_.TryGet<Sa11xxMcpCodec>()) codec->PostRestore();
    RefreshLine(clock_->Cycles());
}

REGISTER_SERVICE(Sa11xxMcp);
