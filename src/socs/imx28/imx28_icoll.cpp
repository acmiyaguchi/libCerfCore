#include "imx28_icoll.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../execution/engine_bridge.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfengine/arm/arm_jit.h>
#include "imx28_id.h"

namespace cerfcore {

namespace {

/* Section 5.4 memory map, one register per 0x10 bytes. */
constexpr uint32_t kOffVector     = 0x000u;
constexpr uint32_t kOffLevelAck   = 0x010u;
constexpr uint32_t kOffCtrl       = 0x020u;
constexpr uint32_t kOffVbase      = 0x040u;
constexpr uint32_t kOffStat       = 0x070u;
constexpr uint32_t kOffRaw0       = 0x0A0u;
constexpr uint32_t kOffInterrupt0 = 0x120u;
constexpr uint32_t kOffDbgRead0   = 0x1130u;
constexpr uint32_t kOffDbgRead1   = 0x1140u;
constexpr uint32_t kOffDbgFlag    = 0x1150u;
constexpr uint32_t kOffVersion    = 0x11E0u;
constexpr uint32_t kMapEnd        = 0x11F0u;

/* Section 5.4.3 CTRL: reset C003_0000h; VECTOR_PITCH [23:21], BYPASS_FSM [20], NO_NESTING
   [19], ARM_RSE_MODE [18], FIQ_FINAL_ENABLE [17], IRQ_FINAL_ENABLE [16]. */
constexpr uint32_t kCtrlReset       = 0xC0030000u;
constexpr uint32_t kCtrlWritable    = 0xC0FF0000u;
constexpr uint32_t kCtrlPitchShift  = 21u;
constexpr uint32_t kCtrlBypassFsm   = 1u << 20;
constexpr uint32_t kCtrlNoNesting   = 1u << 19;
constexpr uint32_t kCtrlArmRse      = 1u << 18;
constexpr uint32_t kCtrlFiqFinal    = 1u << 17;
constexpr uint32_t kCtrlIrqFinal    = 1u << 16;

/* Section 5.4.4 VBASE TABLE_ADDRESS [31:2]; 5.4.5 STAT reset 7Fh, VECTOR_NUMBER [6:0]. */
constexpr uint32_t kVbaseMask  = 0xFFFFFFFCu;
constexpr uint32_t kStatReset  = 0x7Fu;

/* Section 5.4.10 INTERRUPTn: ENFIQ [4], SOFTIRQ [3], ENABLE [2], PRIORITY [1:0]. */
constexpr uint32_t kIntEnFiq    = 1u << 4;
constexpr uint32_t kIntSoftIrq  = 1u << 3;
constexpr uint32_t kIntEnable   = 1u << 2;
constexpr uint32_t kIntPriority = 0x3u;
constexpr uint32_t kIntWritable = 0x1Fu;

/* Sections 5.4.139, 5.4.140, 5.4.141, 5.4.146. */
constexpr uint32_t kDbgRead0Value = 0xECA94567u;
constexpr uint32_t kDbgRead1Value = 0x1356DA98u;
constexpr uint32_t kDbgFlagMask   = 0xFFFFu;
constexpr uint32_t kVersionValue  = 0x03010000u;

enum class Kind { kNone, kVector, kLevelAck, kCtrl, kVbase, kStat, kRaw, kInterrupt,
                  kDbgRead, kDbgFlag, kVersion };

struct Decoded {
    Kind     kind = Kind::kNone;
    uint32_t n    = 0u;
    bool     sct  = false;
};

/* Section 5.4 memory map Access "R": STAT, RAW0-3, DBGREAD0-1 and VERSION; section 39.3 "The SCT
   registers are not implemented if the primary register is read-only". LEVELACK (5.4.2) has no
   SCT addresses. */
Decoded Decode(uint32_t reg) {
    Decoded d;
    d.sct = true;
    if (reg == kOffVector)   { d.kind = Kind::kVector; return d; }
    if (reg == kOffLevelAck) { d.kind = Kind::kLevelAck; d.sct = false; return d; }
    if (reg == kOffCtrl)     { d.kind = Kind::kCtrl; return d; }
    if (reg == kOffVbase)    { d.kind = Kind::kVbase; return d; }
    if (reg == kOffStat)     { d.kind = Kind::kStat; d.sct = false; return d; }
    if (reg >= kOffRaw0 && reg < kOffRaw0 + 0x40u) {
        d.kind = Kind::kRaw;
        d.sct  = false;
        d.n    = (reg - kOffRaw0) >> 4;
        return d;
    }
    if (reg >= kOffInterrupt0 && reg < kOffInterrupt0 + 0x800u) {
        d.kind = Kind::kInterrupt;
        d.n    = (reg - kOffInterrupt0) >> 4;
        return d;
    }
    if (reg == kOffDbgRead0 || reg == kOffDbgRead1) {
        d.kind = Kind::kDbgRead;
        d.sct  = false;
        d.n    =(reg - kOffDbgRead0) >> 4;
        return d;
    }
    if (reg == kOffDbgFlag)  { d.kind = Kind::kDbgFlag; return d; }
    if (reg == kOffVersion)  { d.kind = Kind::kVersion; d.sct = false; return d; }
    return d;
}

}  /* namespace */

bool Imx28Icoll::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

void Imx28Icoll::OnReady() {
    jit_ = &emu_.Get<EngineBridge>().Engine().Get<cerfengine::ArmJit>();
    Reset();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        std::lock_guard<std::mutex> guard(state_mtx_);
        Reset();
        Notify();
    });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

/* Section 5.4 memory map "Reset value" column. */
void Imx28Icoll::Reset() {
    ctrl_            = kCtrlReset;
    vbase_           = 0u;
    dbgflag_         = 0u;
    inservice_       = 0u;
    held_vector_     = 0u;
    held_number_     = 0u;
    presented_level_ = 0u;
    pending_         = false;
    stat_valid_      = false;
    for (uint8_t& r : interrupt_) r = 0u;
}

bool Imx28Icoll::Requested(uint32_t n) const {
    return ((raw_[n >> 5] >> (n & 31u)) & 1u) != 0u || (interrupt_[n] & kIntSoftIrq) != 0u;
}

/* Section 5.2.2: "When an interrupt source is programmed as an FIQ, and IRQ cannot be
   generated from that source." */
bool Imx28Icoll::Candidate(uint32_t n, bool fiq) const {
    return Requested(n) && (interrupt_[n] & kIntEnable) != 0u &&
           ((interrupt_[n] & kIntEnFiq) != 0u) == fiq;
}

uint32_t Imx28Icoll::CandidateWord(uint32_t word, bool fiq) const {
    uint32_t bits = 0u;
    for (uint32_t b = 0; b < 32u; ++b) {
        if (Candidate(word * 32u + b, fiq)) bits |= 1u << b;
    }
    return bits;
}

/* Section 5.2.4: the wake-up condition ORs "all enabled interrupt requests including those that
   are directed to the FIQ CPU input". */
bool Imx28Icoll::AnyEnabledRequest() const {
    for (uint32_t n = 0; n < kSources; ++n) {
        if (Requested(n) && (interrupt_[n] & kIntEnable) != 0u) return true;
    }
    return false;
}

/* Section 5.4.3 VECTOR_PITCH: "0x0 and 0x1 yield a multiplier of 4 bytes", 0x2 8 bytes, up to 0x7 28 bytes. */
uint32_t Imx28Icoll::Pitch() const {
    const uint32_t field = (ctrl_ >> kCtrlPitchShift) & 0x7u;
    return (field < 1u ? 1u : field) * 4u;
}

/* Section 5.2: same-level sources go "in a strict linear priority order within level from lowest
   to highest interrupt source bit number"; 5.2.1: only "at a level higher than any currently
   in-service level"; 5.4.3 NO_NESTING. */
void Imx28Icoll::Evaluate() {
    if (!Operating()) {
        pending_ = false;
        return;
    }
    if (pending_) return;
    int32_t best_n = -1;
    int32_t best_l = -1;
    for (uint32_t n = 0; n < kSources; ++n) {
        if (!Candidate(n, false)) continue;
        const int32_t l = static_cast<int32_t>(interrupt_[n] & kIntPriority);
        if (l > best_l) {
            best_l = l;
            best_n = static_cast<int32_t>(n);
        }
    }
    if (best_n < 0) return;
    const bool blocked = (ctrl_ & kCtrlNoNesting) != 0u
                             ? inservice_ != 0u
                             : (inservice_ >> static_cast<uint32_t>(best_l)) != 0u;
    if (blocked) return;
    pending_         = true;
    presented_level_ = static_cast<uint32_t>(best_l);
    held_number_     = static_cast<uint32_t>(best_n);
    held_vector_     = vbase_ + Pitch() * held_number_;
    stat_valid_      = true;
}

void Imx28Icoll::Notify() {
    const bool operating = Operating();
    if (operating && (ctrl_ & kCtrlFiqFinal) != 0u) {
        for (uint32_t w = 0; w < kWords; ++w) {
            if (CandidateWord(w, true) != 0u) {
                emu_.Get<Fatal>().Die("Imx28Icoll: FIQ asserted (word %u candidates %08X)", w,
                                      CandidateWord(w, true));
            }
        }
    }
    if (operating && pending_ && (ctrl_ & kCtrlIrqFinal) != 0u) jit_->SetInterruptPending();
    else                                                         jit_->ClearInterruptPending();
    jit_->SetIdleWake(operating && AnyEnabledRequest());
}

void Imx28Icoll::AssertSource(uint32_t source) {
    if (source >= kSources) emu_.Get<Fatal>().Die("Imx28Icoll: AssertSource %u", source);
    std::lock_guard<std::mutex> guard(state_mtx_);
    raw_[source >> 5] |= 1u << (source & 31u);
    Evaluate();
    Notify();
}

void Imx28Icoll::DeassertSource(uint32_t source) {
    if (source >= kSources) emu_.Get<Fatal>().Die("Imx28Icoll: DeassertSource %u", source);
    std::lock_guard<std::mutex> guard(state_mtx_);
    raw_[source >> 5] &= ~(1u << (source & 31u));
    Evaluate();
    Notify();
}

/* Section 5.2.1: "Software indicates the in-service state by writing to the HW_ICOLL_VECTOR
   register"; with ARM_RSE_MODE the read does it. The FSM then "reopens the holding register
   and scans for new interrupt sources". */
void Imx28Icoll::EnterService() {
    if (!pending_) {
        emu_.Get<Fatal>().Die("Imx28Icoll: in-service notification with no interrupt presented "
                              "(CTRL %08X inservice %X)", ctrl_, inservice_);
    }
    inservice_ |= 1u << presented_level_;
    pending_ = false;
    Evaluate();
    Notify();
}

/* Section 5.4.2: "the value written to the levelack register is decoded not binary, i.e. 8, 4,
   2, 1"; the interrupt collector "resets the in-service bit for that level". */
void Imx28Icoll::AcknowledgeLevel(uint32_t value) {
    const uint32_t level = value & 0xFu;
    if (level == 0u || (level & (level - 1u)) != 0u || (inservice_ & level) == 0u) {
        emu_.Get<Fatal>().Die("Imx28Icoll: LEVELACK %X with in-service levels %X", value,
                              inservice_);
    }
    inservice_ &= ~level;
    Evaluate();
    Notify();
}


void Imx28Icoll::WriteCtrl(Imx28SctOp op, uint32_t value) {
    const uint32_t old  = ctrl_;
    uint32_t       next = Imx28ApplySct(op, old, value);
    next = (old & ~kCtrlWritable) | (next & kCtrlWritable);
    if ((next & kCtrlBypassFsm) != 0u) {
        emu_.Get<Fatal>().Die("Imx28Icoll: CTRL %08X sets BYPASS_FSM", next);
    }
    if (IsSoftReset(old, next)) {
        Reset();
        Notify();
        return;
    }
    ctrl_ = next;
    Evaluate();
    Notify();
}

/* Section 5.4.10: "Modifying the priority of an enabled interrupt may result in undefined
   behavior." */
void Imx28Icoll::WriteInterrupt(uint32_t n, Imx28SctOp op, uint32_t value) {
    const uint32_t old  = interrupt_[n];
    const uint32_t next = Imx28ApplySct(op, old, value) & kIntWritable;
    if ((old & kIntEnable) != 0u && ((old ^ next) & kIntPriority) != 0u) {
        emu_.Get<Fatal>().Die("Imx28Icoll: INTERRUPT%u priority %u -> %u while enabled", n,
                              old & kIntPriority, next & kIntPriority);
    }
    interrupt_[n] = static_cast<uint8_t>(next);
    Evaluate();
    Notify();
}

uint32_t Imx28Icoll::ComposeRead(uint32_t index, uint32_t addr) {
    const Decoded d = Decode(index);
    switch (d.kind) {
        case Kind::kVector: {
            const uint32_t v = held_vector_;
            if ((ctrl_ & kCtrlArmRse) != 0u) EnterService();
            return v;
        }
        case Kind::kLevelAck:  return 0u;
        case Kind::kCtrl:      return ctrl_;
        case Kind::kVbase:     return vbase_;
        case Kind::kStat:      return stat_valid_ ? held_number_ : kStatReset;
        case Kind::kRaw:       return raw_[d.n];
        case Kind::kInterrupt: return interrupt_[d.n];
        case Kind::kDbgRead:   return d.n == 0u ? kDbgRead0Value : kDbgRead1Value;
        case Kind::kDbgFlag:   return dbgflag_;
        case Kind::kVersion:   return kVersionValue;
        default:               break;
    }
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

uint32_t Imx28Icoll::ReadLocked(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("ReadWord", addr, 0);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    const Decoded        d = Decode(a.reg_offset);
    if (d.kind == Kind::kNone || (a.op != Imx28SctOp::Write && !d.sct)) {
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }
    if (a.op != Imx28SctOp::Write) return 0u;
    return ComposeRead(a.reg_offset, addr);
}

uint32_t Imx28Icoll::ReadWord(uint32_t addr) {
    std::lock_guard<std::mutex> guard(state_mtx_);
    return ReadLocked(addr);
}

uint32_t Imx28Icoll::PeekLanes(uint32_t word_addr, uint32_t) {
    std::lock_guard<std::mutex> guard(state_mtx_);
    const uint32_t off = word_addr - MmioBase();
    if (Decode(off & ~0xFu).kind == Kind::kVector) return held_vector_;
    return ReadLocked(word_addr);
}

void Imx28Icoll::WriteLocked(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("WriteWord", addr, value);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    const Decoded        d = Decode(a.reg_offset);
    if (d.kind == Kind::kNone || (a.op != Imx28SctOp::Write && !d.sct)) {
        HaltUnsupportedAccess("WriteWord", addr, value);
    }
    if (d.kind == Kind::kCtrl) {
        WriteCtrl(a.op, value);
        return;
    }
    RequireOperating("WriteWord", ctrl_, addr, value);
    switch (d.kind) {
        case Kind::kVector:
            if ((ctrl_ & kCtrlArmRse) != 0u) HaltUnsupportedAccess("WriteWord-RSE", addr, value);
            EnterService();
            return;
        case Kind::kLevelAck:  AcknowledgeLevel(value); return;
        case Kind::kVbase:
            vbase_ = Imx28ApplySct(a.op, vbase_, value) & kVbaseMask;
            return;
        case Kind::kInterrupt: WriteInterrupt(d.n, a.op, value); return;
        case Kind::kDbgFlag:
            dbgflag_ = Imx28ApplySct(a.op, dbgflag_, value) & kDbgFlagMask;
            return;
        default:               break;
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

void Imx28Icoll::WriteWord(uint32_t addr, uint32_t value) {
    std::lock_guard<std::mutex> guard(state_mtx_);
    WriteLocked(addr, value);
}

void Imx28Icoll::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> guard(state_mtx_);
    w.WriteBytes("raw", raw_, sizeof(raw_));
    w.WriteBytes("interrupt", interrupt_, sizeof(interrupt_));
    w.Write("ctrl", ctrl_);
    w.Write("vbase", vbase_);
    w.Write("dbgflag", dbgflag_);
    w.Write("inservice", inservice_);
    w.Write("held_vector", held_vector_);
    w.Write("held_number", held_number_);
    w.Write("presented_level", presented_level_);
    w.Write<uint8_t>("pending", pending_ ? 1u : 0u);
    w.Write<uint8_t>("stat_valid", stat_valid_ ? 1u : 0u);
}

void Imx28Icoll::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> guard(state_mtx_);
    r.ReadBytes("raw", raw_, sizeof(raw_));
    r.ReadBytes("interrupt", interrupt_, sizeof(interrupt_));
    r.Read("ctrl", ctrl_);
    r.Read("vbase", vbase_);
    r.Read("dbgflag", dbgflag_);
    r.Read("inservice", inservice_);
    r.Read("held_vector", held_vector_);
    r.Read("held_number", held_number_);
    r.Read("presented_level", presented_level_);
    uint8_t pending = 0, valid = 0;
    r.Read("pending", pending);
    r.Read("stat_valid", valid);
    pending_    = pending != 0u;
    stat_valid_ = valid != 0u;
}

void Imx28Icoll::PostRestore() {
    std::lock_guard<std::mutex> guard(state_mtx_);
    Notify();
}

REGISTER_SERVICE(Imx28Icoll);

}
