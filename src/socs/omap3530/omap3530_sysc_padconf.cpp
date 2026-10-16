#include "omap3530_sysc_padconf.h"

#include <cerfcore/boards/board_context.h>
#include "omap3530_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "omap3530_scm_reset_image.h"

namespace {

constexpr uint32_t kOffDevconf0 = 0x274u;
constexpr uint32_t kOffDevconf1 = 0x2D8u;

/* SPRUF98Y Table 7-80 (printed p. 881): CONTROL_DEVCONF0 MCBSP2_CLKS [6], MCBSP1_FSR [4], MCBSP1_CLKR
   [3], MCBSP1_CLKS [2]; Table 7-96 (printed p. 892-893): CONTROL_DEVCONF1 MCBSP5_CLKS [4], MCBSP4_CLKS
   [2], MCBSP3_CLKS [0]; a CLKS bit 1 selects the external pin McBSP_CLKS. */
constexpr uint32_t kMcBsp1Fsr  = 1u << 4;
constexpr uint32_t kMcBsp1Clkr = 1u << 3;

/* SPRUF98Y Figure 7-7 (page 769): MUXMODE [2:0] of each 16-bit pad configuration field. */
constexpr uint16_t kMuxMode = 0x7u;

/* SPRUF98Y Table 7-75 (page 868): PULLUDENABLE [3] 1 = pull enabled; PULLTYPESELECT [4] 1 = pull-up. */
constexpr uint16_t kPullEnable = 1u << 3;
constexpr uint16_t kPullUp     = 1u << 4;

bool InWord(uint32_t off, uint32_t word) { return off >= word && off < word + 4u; }

}

bool Omap3530SyscPadconf::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Omap3530;
}

/* OMAP35x TRM SPRUF98Y section 7.3.1.2 (page 765): "The SCM responds only to the internal power-on reset and to the
   device type"; "the SCM is not affected by a warm reset." */
void Omap3530SyscPadconf::OnReady() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        LoadPowerOnLocked();
    }
    auto& reset = emu_.Get<GuestCpuReset>();
    reset.RegisterResetListener([this](ResetLineKind kind) {
        if (kind != ResetLineKind::Rtc) return;
        std::lock_guard<std::mutex> lk(mu_);
        const uint32_t devconf0 = Word(kOffDevconf0);
        const uint32_t devconf1 = Word(kOffDevconf1);
        LoadPowerOnLocked();
        devconf_changed_ = Word(kOffDevconf0) != devconf0 || Word(kOffDevconf1) != devconf1;
        pads_reset_      = true;
    });
    reset.RegisterResetReleaseListener([this] {
        if (pads_reset_) {
            pads_reset_ = false;
            for (auto& l : pad_listeners_) l.fn();
        }
        if (!devconf_changed_) return;
        devconf_changed_ = false;
        NotifyDevconf();
    });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void Omap3530SyscPadconf::LoadPowerOnLocked() {
    regs_.fill(0u);
    for (const Omap3530RegReset& r : omap3530::kScmPowerOn) StoreWordLocked(r.off, r.value);
}

void Omap3530SyscPadconf::StoreWordLocked(uint32_t off, uint32_t value) {
    regs_[off / 2u]      = static_cast<uint16_t>(value);
    regs_[off / 2u + 1u] = static_cast<uint16_t>(value >> 16);
}

uint16_t Omap3530SyscPadconf::ReadHalf(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off & 1u) HaltUnsupportedAccess("ReadHalf (misaligned)", addr, 0);
    std::lock_guard<std::mutex> lk(mu_);
    const uint16_t value = regs_[off / 2];
    LOG(Periph, "[SYSC_PADCONF] R16 0x%03X -> 0x%04X\n", off, value);
    return value;
}

uint32_t Omap3530SyscPadconf::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off & 3u) HaltUnsupportedAccess("ReadWord (misaligned)", addr, 0);
    std::lock_guard<std::mutex> lk(mu_);
    const uint32_t value = Word(off);
    LOG(Periph, "[SYSC_PADCONF] R32 0x%03X -> 0x%08X\n", off, value);
    return value;
}

void Omap3530SyscPadconf::WriteHalf(uint32_t addr, uint16_t value) {
    const uint32_t off = addr - MmioBase();
    if (off & 1u) HaltUnsupportedAccess("WriteHalf (misaligned)", addr, value);
    {
        std::lock_guard<std::mutex> lk(mu_);
#if CERF_DEV_MODE
        LOG(Periph, "[SYSC_PADCONF] W16 0x%03X <- 0x%04X\n", off, value);
#endif
        regs_[off / 2] = value;
    }
    AfterWrite(off, 2u);
}

void Omap3530SyscPadconf::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off & 3u) HaltUnsupportedAccess("WriteWord (misaligned)", addr, value);
    {
        std::lock_guard<std::mutex> lk(mu_);
#if CERF_DEV_MODE
        LOG(Periph, "[SYSC_PADCONF] W32 0x%03X <- 0x%08X\n", off, value);
#endif
        StoreWordLocked(off, value);

        constexpr uint32_t kPadconfOffOffset = 0x270u;
        constexpr uint32_t kGpStatusOffset   = 0x2F4u;
        constexpr uint32_t kStartSaveBit     = 1u << 1;
        constexpr uint16_t kSaveDoneBit      = 1u << 0;
        if (off == kPadconfOffOffset && (value & kStartSaveBit)) {
            regs_[kGpStatusOffset / 2] |= kSaveDoneBit;
        }
    }
    AfterWrite(off, 4u);
}

uint32_t Omap3530SyscPadconf::Word(uint32_t off) const {
    return static_cast<uint32_t>(regs_[off / 2]) | (static_cast<uint32_t>(regs_[off / 2 + 1]) << 16);
}

void Omap3530SyscPadconf::AfterWrite(uint32_t off, uint32_t bytes) {
    if (InWord(off, kOffDevconf0) || InWord(off, kOffDevconf1)) NotifyDevconf();
    for (auto& l : pad_listeners_) {
        if (l.pad_off >= off && l.pad_off < off + bytes) l.fn();
    }
}

void Omap3530SyscPadconf::NotifyDevconf() {
    for (auto& fn : devconf_listeners_) fn();
}

bool Omap3530SyscPadconf::McBspClksFromPad(uint32_t port) const {
    std::lock_guard<std::mutex> lk(mu_);
    switch (port) {
    case 1: return (Word(kOffDevconf0) >> 2) & 1u;
    case 2: return (Word(kOffDevconf0) >> 6) & 1u;
    case 3: return (Word(kOffDevconf1) >> 0) & 1u;
    case 4: return (Word(kOffDevconf1) >> 2) & 1u;
    case 5: return (Word(kOffDevconf1) >> 4) & 1u;
    }
    emu_.Get<Fatal>().Die("Omap3530SyscPadconf: McBSP port %u has no CLKS select", port);
}

bool Omap3530SyscPadconf::McBsp1ClkrFromClkx() const {
    std::lock_guard<std::mutex> lk(mu_);
    return (Word(kOffDevconf0) & kMcBsp1Clkr) != 0u;
}

bool Omap3530SyscPadconf::McBsp1FsrFromFsx() const {
    std::lock_guard<std::mutex> lk(mu_);
    return (Word(kOffDevconf0) & kMcBsp1Fsr) != 0u;
}

void Omap3530SyscPadconf::RegisterDevconfListener(std::function<void()> fn) {
    devconf_listeners_.push_back(std::move(fn));
}

uint32_t Omap3530SyscPadconf::PadMuxMode(uint32_t pad_off) const {
    std::lock_guard<std::mutex> lk(mu_);
    return regs_[pad_off / 2u] & kMuxMode;
}

Omap3530SyscPadconf::PadPull Omap3530SyscPadconf::PadPullOf(uint32_t pad_off) const {
    std::lock_guard<std::mutex> lk(mu_);
    const uint16_t field = regs_[pad_off / 2u];
    if ((field & kPullEnable) == 0u) return PadPull::None;
    return (field & kPullUp) != 0u ? PadPull::Up : PadPull::Down;
}

void Omap3530SyscPadconf::RegisterPadListener(uint32_t pad_off, std::function<void()> fn) {
    pad_listeners_.push_back(PadListener{pad_off, std::move(fn)});
}

void Omap3530SyscPadconf::SaveState(StateWriter& w) {
    w.WriteBytes("regs", regs_.data(), regs_.size() * sizeof(uint16_t));
}

void Omap3530SyscPadconf::RestoreState(StateReader& r) {
    r.ReadBytes("regs", regs_.data(), regs_.size() * sizeof(uint16_t));
}

REGISTER_SERVICE(Omap3530SyscPadconf);
