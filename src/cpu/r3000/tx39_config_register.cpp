#include "tx39_config_register.h"

#include <string_view>

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../execution/mips_engine_bridge.h"
#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/mips/mips_cpu_state.h>
#include <cerfengine/mips/mips_exception_delivery.h>
#include <cerfengine/mips/mips_interrupt_channel.h>
#include "../../socs/pr31x00/pr31500_id.h"
#include "../../socs/pr31x00/pr31700_id.h"

namespace cerfcore {

REGISTER_SERVICE(Tx39ConfigRegister);

namespace {

/* TX39 Config (TMPR39xx-um Fig 6-10): ICS<21:19> DCS<18:16> read-only; RF<11:10> Doze<9>
   Halt<8> Lock<7> DCBR<6> ICE<5> DCE<4> IRSize<3:2> DRSize<1:0>; 31:22 and 15:12 read 0. */
constexpr uint32_t kReadOnly = 0x003F0000u;
constexpr uint32_t kWritable = 0x00000FFFu;
constexpr uint32_t kRfShift  = 10u;
constexpr uint32_t kRfMask   = 0x3u;
constexpr uint32_t kDoze     = 1u << 9;
constexpr uint32_t kHalt     = 1u << 8;
constexpr uint32_t kLock     = 1u << 7;
/* TMPR39xx-um Fig 6-10 p62-63 value on reset (ICE 1, DCE 1, rest 0); ICS 010 / DCS 000 for the
   4 KB I-cache and 1 KB D-cache (PR31700.PDF p20, PR31500.PDF p4). */
constexpr uint32_t kResetValue = 0x00100030u;

}  // namespace

bool Tx39ConfigRegister::ShouldRegister() {
    const std::string_view soc = emu_.Get<BoardContext>().GetSocId();
    return soc == SocId::Pr31500 || soc == SocId::Pr31700;
}

void Tx39ConfigRegister::OnReady() {
    MipsEngineBridge& bridge = emu_.Get<MipsEngineBridge>();
    engine_    = &bridge.Engine();
    cpu_state_ = engine_->Get<cerfengine::MipsCpu>().State();
    bridge.RegisterResetListener([this] { ApplyReset(); });
    ApplyReset();
}

void Tx39ConfigRegister::ApplyReset() { cpu_state_->cp0_config = kResetValue; }

uint32_t Tx39ConfigRegister::ReducedFrequency() const {
    return (cpu_state_->cp0_config >> kRfShift) & kRfMask;
}

void Tx39ConfigRegister::RegisterReducedFrequencyListener(std::function<void()> fn) {
    rf_listeners_.push_back(std::move(fn));
}

/* Lock: "Setting this bit to 1 prevents further writes to the Config register"; a store that
   sets Lock with other bits keeps the other settings (TMPR39xx-um Fig 6-10). */
void CERFENGINE_FASTCALL Tx39ConfigRegister::WriteConfig(void* context, uint32_t value) {
    Tx39ConfigRegister* reg = static_cast<Tx39ConfigRegister*>(context);
    cerfengine::MipsCpuState& s = *reg->cpu_state_;
    if ((s.cp0_config & kLock) != 0u) return;
    /* TMPR39xx-um Figure 6-10 (p63): "Operation is undefined when both Doze bit and Halt bit
       are set to 1." */
    if ((value & (kDoze | kHalt)) == (kDoze | kHalt)) {
        reg->emu_.Get<Fatal>().Die("Tx39ConfigRegister: Config write 0x%08X sets Doze and Halt",
                                   value);
    }
    const uint32_t old_rf = reg->ReducedFrequency();
    s.cp0_config = (s.cp0_config & kReadOnly) | (value & kWritable & ~(kDoze | kHalt));
    if (reg->ReducedFrequency() != old_rf) {
        for (auto& fn : reg->rf_listeners_) fn();
    }
    if ((value & (kDoze | kHalt)) == 0u) return;
    /* TMPR39xx-um §5.1 (p.231): Halt mode will "stall the pipeline in holding current status";
       the processor "can be returned from Halt mode to Active mode, and the Halt bit cleared to 0,
       by asserting the INT[5:0], NMI or RESET signals". */
    reg->engine_->Get<cerfengine::MipsInterruptChannel>().HaltCore();
    reg->engine_->Get<cerfengine::MipsExceptionDelivery>().LeaveBlock();
}

}
