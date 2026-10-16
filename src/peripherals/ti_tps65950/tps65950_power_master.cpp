#include "tps65950_power_master.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/ti_omap35x_evm/ti_omap35x_evm_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "tps65950_board.h"
#include "tps65950_reset_image.h"
#include "twl4030.h"

#include <cstring>

namespace {

/* TPS65950 TRM SWCU050E Table 2-4 (page 81) PM_MASTER, Table 2-6 (page 82) PM_RECEIVER, Table 2-6 (page 84)
   VDD1_VMODE_CFG 0xBA and VDD2_VMODE_CFG 0xC8. */
constexpr uint8_t kP1SwEvents = 0x46u, kP3SwEvents = 0x48u, kPbWordMsb = 0x4Bu, kPbWordLsb = 0x4Cu;
constexpr uint8_t kSeqAddWarm = 0x58u, kMemoryAddress = 0x59u, kMemoryData = 0x5Au;
constexpr uint8_t kScConfig = 0x5Bu, kWatchdogCfg = 0x5Eu, kItCheckCfg = 0x5Fu, kVibratorCfg = 0x60u;
constexpr uint8_t kVdd1VmodeCfg = 0xBAu, kVdd2VmodeCfg = 0xC8u;

/* SWCU050E Table 5-79 (page 292): ENABLE_WARMRESET [4], LVL_WAKEUP [3], every field reset 0. */
constexpr uint8_t kEnableWarmReset = 1u << 4, kLvlWakeup = 1u << 3;

constexpr uint8_t kBbCfg = 0x6Du, kBbchen = 1u << 4;

/* SWCU050E Table 5-25 (page 258): words 0x00-0x0E read-only but for the next-address byte, 0x1F-0x2A and the
   end-of-sequence word 0x3F read-only, 0x2B-0x3E read / write; Table 5-119 (page 303) MEMORY_DATA reset 0x18. */
constexpr uint8_t kEndOfSequence   = 0x3Fu;
constexpr uint8_t kMemoryDataReset = 0x18u;

/* SWCU050E Table 5-75 (page 290) PROTECT_KEY: "Write C0, then 0C enable write access to power configuration registers";
   "When KEY_CFG = 0, no write in all protected register bits. Read is still possible."; Linux TWL4030 driver
   TWL4030_PM_MASTER_KEY_TST1 0xe0, TWL4030_PM_MASTER_KEY_TST2 0x0e. */
constexpr uint8_t kProtectKey = 0x44u;
constexpr uint8_t kKeyCfg1 = 0xC0u, kKeyCfg2 = 0x0Cu, kKeyTst1 = 0xE0u, kKeyTst2 = 0x0Eu;
struct Protected {
    uint8_t sub;
    uint8_t bits;
    bool    test;
};
constexpr Protected kProtected[] = {
    {0x36u, 0xFFu, false}, {0x37u, 0xFFu, false}, {0x38u, 0xFFu, false}, {0x39u, 0xFFu, false}, {0x3Bu, 0xFFu, false},
    {0x3Fu, 0xBFu, true},  {0x40u, 0xFFu, true},  {0x42u, 0xFFu, false}, {0x51u, 0xFFu, false}, {0x52u, 0xFFu, false},
    {0x53u, 0xFFu, false}, {0x54u, 0xFFu, false}, {0x55u, 0xFFu, false}, {0x56u, 0xFFu, false}, {0x57u, 0xFFu, false},
    {0x58u, 0xFFu, false}, {0x59u, 0xFFu, false}, {0x5Au, 0xFFu, false}, {0x5Fu, 0x70u, true},  {0x62u, 0xFFu, true},
    {0x63u, 0xFFu, true},  {0x64u, 0xFFu, true},  {0x65u, 0xFFu, true},  {0x66u, 0xFFu, true},  {0x67u, 0xFFu, true},
    {0x69u, 0xFFu, true},  {0x6Au, 0xFFu, true},  {0x6Bu, 0xFFu, true},  {0x6Cu, 0xFFu, true},  {0x6Fu, 0xFFu, true},
    {0x70u, 0xFFu, true},  {0x75u, 0x70u, true},  {0x79u, 0x70u, true},  {0x7Du, 0x70u, true},  {0x81u, 0x70u, true},
    {0x85u, 0x70u, true},  {0x89u, 0x70u, true},  {0x8Du, 0x70u, true},  {0x91u, 0x70u, true},  {0x95u, 0x70u, true},
    {0x99u, 0x70u, true},  {0x9Du, 0x70u, true},  {0xA1u, 0x70u, true},  {0xA5u, 0x70u, true},  {0xABu, 0xFFu, true},
    {0xACu, 0xFFu, true},  {0xB5u, 0xFFu, true},  {0xB6u, 0xFFu, true},  {0xC3u, 0xFFu, true},  {0xC4u, 0xFFu, true},
};

bool Writable(uint8_t address) {
    const uint8_t word = static_cast<uint8_t>(address >> 2);
    if (word <= 0x0Eu) return (address & 3u) == 3u;
    return word < 0x1Fu || (word >= 0x2Bu && word < kEndOfSequence);
}

/* SWCU050E Table 5-18 (page 254): DEV_GR [2:0], MT, RES_ID [7:0], RES_STATE [3:0]; Table 5-8 (page 248, page 249):
   TRITON_RESET is resource 27, assigned to P3, P2 and P1; page 241: WAIT-ON 0000, ACTIVE 1110. */
constexpr uint32_t kBroadcast    = 1u << 12;
constexpr uint32_t kTritonReset  = 27u;
constexpr uint32_t kStateOff     = 0x0u, kStateActive = 0xEu;
constexpr uint32_t kMaxSequence  = 64u;

/* SWCU050E page 241: WARMRESET (1111); Table 2-6 (page 85) TRITON_RESET_DEV_GRP 0xEC; Table 5-411 (page 398) DEV_GRP [7:5]
   reset 0x7; Table 5-8 (page 249): 32KCLKOUT, resource 26, active in P3, P2 and P1 at POR in every boot mode. */
constexpr uint8_t  kTritonResetDevGrp = 0xECu;
constexpr uint8_t  kDevGrpAll = 0xE0u, kWarmCfg = 1u << 4;
constexpr uint32_t kStateWarmReset = 0xFu, k32kClkOut = 26u;

/* SWCU050E Table 2-6 (page 85) 32KCLKOUT_DEV_GRP 0xE9; Table 5-405 (page 397) DEV_GRP [7:5] reset 0x7, WARM_CFG [4] reset 0. */
constexpr uint8_t kClkOut32kDevGrp = 0xE9u;

/* SWCU050E Table 5-8 (page 248, page 249): VPLL1 7, VDD1 15, VDD2 16; Table 5-215 (page 346) VPLL1_DEV_GRP 0x8A, Table 5-221
   (page 347) VPLL1_DEDICATED 0x8D VSEL [2:0], Table 5-291 (page 367) VDD1_DEV_GRP 0xB0, Table 5-309 (page 371) VDD1_VSEL 0xB9,
   Table 5-319 (page 374) VDD2_DEV_GRP 0xBE, Table 5-337 (page 379) VDD2_VSEL 0xC7. */
struct Rail {
    uint32_t resource;
    uint8_t  dev_grp;
    uint8_t  vsel;
    uint8_t  vsel_mask;
    uint8_t  vsel_default;
};

consteval Rail MakeRail(uint32_t resource, uint8_t dev_grp, uint8_t vsel, uint8_t vsel_mask) {
    size_t i = 0;
    while (tps65950::kMc021PowerOn[i].sub != vsel) ++i;
    return {resource, dev_grp, vsel, vsel_mask, tps65950::kMc021PowerOn[i].value};
}

constexpr Rail kRails[3] = {MakeRail(7u, 0x8Au, 0x8Du, 0x07u), MakeRail(15u, 0xB0u, 0xB9u, 0x7Fu),
                            MakeRail(16u, 0xBEu, 0xC7u, 0x7Fu)};

}

bool Tps65950PowerMaster::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::TiOmap35xEvm;
}

/* TPS65950 datasheet SWCS032F Table 5-24 (page 60): BOOT0 1, BOOT1 0 = MC021, Master_C021_Generic. */
void Tps65950PowerMaster::OnReady() {
    twl_ = &emu_.Get<Twl4030>();
    const Tps65950BootStraps straps = emu_.Get<Tps65950Board>().BootStraps();
    if (!straps.boot0 || straps.boot1) {
        emu_.Get<Fatal>().Die("Tps65950Power: BOOT0 %d, BOOT1 %d; the power-on resource mapping of that boot mode is not "
                              "modelled", straps.boot0 ? 1 : 0, straps.boot1 ? 1 : 0);
    }
    PowerOn();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) { OnReset(kind == ResetLineKind::Rtc); });
}

/* SWCU050E Table 5-117 (page 303): MEMORY_ADDRESS selects a 6-bit word and a 2-bit byte; Table 5-119: "The
   MEMORY_ADDRESS is automatically incremented". */
uint8_t Tps65950PowerMaster::NextAddress() {
    const uint8_t address = twl_->PowerReg(kMemoryAddress);
    twl_->SetPowerReg(kMemoryAddress, static_cast<uint8_t>(address + 1u));
    return address;
}

void Tps65950PowerMaster::WriteMemoryData(uint8_t value) {
    const uint8_t address = NextAddress();
    if (!Writable(address)) return;
    memory_[address] = value;
    known_[address]  = 1u;
}

uint8_t Tps65950PowerMaster::ReadMemoryData() {
    const uint8_t address = NextAddress();
    return known_[address] != 0u ? memory_[address] : 0u;
}

bool Tps65950PowerMaster::ConsumeWrite(uint8_t sub, uint8_t& value) {
    if (sub == kProtectKey) {
        WriteProtectKey(value);
        return true;
    }
    const uint8_t locked = LockedBits(sub);
    if (sub == kMemoryData) {
        if (locked == 0u) WriteMemoryData(value);
        return true;
    }
    value = static_cast<uint8_t>((twl_->PowerReg(sub) & locked) | (value & ~locked));
    return false;
}

bool Tps65950PowerMaster::ConsumeRead(uint8_t sub, uint8_t& value) {
    if (sub != kMemoryData) return false;
    value = ReadMemoryData();
    return true;
}

void Tps65950PowerMaster::WriteProtectKey(uint8_t value) {
    const uint8_t armed = key_armed_;
    key_armed_          = 0u;
    if (armed == kKeyCfg1 && value == kKeyCfg2) {
        key_cfg_ = true;
    } else if (armed == kKeyTst1 && value == kKeyTst2) {
        key_tst_ = true;
    } else if (value == kKeyCfg1 || value == kKeyTst1) {
        key_armed_ = value;
    } else {
        key_cfg_ = false;
        key_tst_ = false;
    }
    StoreKeyRead();
}

uint8_t Tps65950PowerMaster::LockedBits(uint8_t sub) const {
    for (const Protected& p : kProtected) {
        if (p.sub == sub) return (p.test ? key_tst_ : key_cfg_) ? 0u : p.bits;
    }
    return 0u;
}

void Tps65950PowerMaster::StoreKeyRead() {
    twl_->SetPowerReg(kProtectKey, static_cast<uint8_t>((key_cfg_ ? 2u : 0u) | (key_tst_ ? 1u : 0u)));
}

/* SWCU050E Table 5-4 (page 242, page 243): the register bits that reset to their default values during a warm reset;
   page 320 IT_CHECK_CFG SC_STATUS [3] and page 330 BB_CFG BBCHEN [4]: "Reset when signal triton_reset_na is low". */
void Tps65950PowerMaster::ResetPowerBits() {
    for (uint8_t r = kP1SwEvents; r <= kP3SwEvents; ++r) twl_->SetPowerReg(r, twl_->PowerReg(r) & ~kLvlWakeup);
    twl_->SetPowerReg(kPbWordMsb, 0xFFu);
    twl_->SetPowerReg(kPbWordLsb, 0xFFu);
    twl_->SetPowerReg(kMemoryAddress, 0u);
    twl_->SetPowerReg(kScConfig, 0u);
    twl_->SetPowerReg(kWatchdogCfg, twl_->PowerReg(kWatchdogCfg) & ~0x1Fu);
    twl_->SetPowerReg(kItCheckCfg, twl_->PowerReg(kItCheckCfg) & ~0x0Fu);
    twl_->SetPowerReg(kVibratorCfg, twl_->PowerReg(kVibratorCfg) & ~0x0Fu);
    twl_->SetPowerReg(kVdd1VmodeCfg, twl_->PowerReg(kVdd1VmodeCfg) & ~0x07u);
    twl_->SetPowerReg(kVdd2VmodeCfg, twl_->PowerReg(kVdd2VmodeCfg) & ~0x07u);
    twl_->SetPowerReg(kBbCfg, twl_->PowerReg(kBbCfg) & ~kBbchen);
}

void Tps65950PowerMaster::PowerOn() {
    std::memset(memory_, 0, sizeof(memory_));
    std::memset(known_, 0, sizeof(known_));
    memory_[0] = kMemoryDataReset;
    known_[0]  = 1u;
    key_cfg_   = false;
    key_tst_   = false;
    key_armed_ = 0u;
    for (const Tps65950RegValue& v : tps65950::kMc021PowerOn) twl_->SetPowerReg(v.sub, v.value);
}

bool Tps65950PowerMaster::InGroups(uint8_t dev_grp_reg, uint8_t groups) const {
    return ((twl_->PowerReg(dev_grp_reg) >> 5) & groups) != 0u;
}

/* SWCU050E Table 5-291 (page 367) WARM_CFG: in the WARMRESET state "the vsel is set back to the default value"; WARM_CFG 1:
   "DEV_GRP is set to 111". */
void Tps65950PowerMaster::WarmResetRail(size_t i) {
    const Rail&   rail = kRails[i];
    const uint8_t vsel = twl_->PowerReg(rail.vsel);
    twl_->SetPowerReg(rail.vsel, static_cast<uint8_t>((vsel & ~rail.vsel_mask) | (rail.vsel_default & rail.vsel_mask)));
    const uint8_t grp = twl_->PowerReg(rail.dev_grp);
    if ((grp & kWarmCfg) != 0u) twl_->SetPowerReg(rail.dev_grp, static_cast<uint8_t>(grp | kDevGrpAll));
}

/* SWCU050E page 241: on NRESWARM the sequence at SEQ_ADD_WARM runs "for the resources assigned to the device group
   for which a warm reset was enabled"; turning TRITON_RESET off and on "resets all the digital logic in the device". */
bool Tps65950PowerMaster::RunWarmSequence() {
    uint8_t groups = 0u;
    for (uint8_t i = 0; i < 3u; ++i) {
        if ((twl_->PowerReg(static_cast<uint8_t>(kP1SwEvents + i)) & kEnableWarmReset) != 0u) {
            groups = static_cast<uint8_t>(groups | (1u << i));
        }
    }
    if (groups == 0u) return false;
    auto&   fatal   = emu_.Get<Fatal>();
    uint8_t word    = static_cast<uint8_t>(twl_->PowerReg(kSeqAddWarm) & 0x3Fu);
    bool    off     = false;
    bool    toggled = false;
    for (uint32_t step = 0; word != kEndOfSequence; ++step) {
        if (step == kMaxSequence) {
            fatal.Die("Tps65950Power: the warm-reset sequence from SEQ_ADD_WARM 0x%02X does not reach the end word 0x3F",
                      twl_->PowerReg(kSeqAddWarm));
        }
        const uint32_t at = word * 4u;
        if (known_[at] == 0u || known_[at + 1u] == 0u || known_[at + 3u] == 0u) {
            fatal.Die("Tps65950Power: warm-reset sequence word 0x%02X holds bytes the guest never wrote; its content "
                      "is not modelled", word);
        }
        const uint32_t message = (static_cast<uint32_t>(memory_[at]) << 8) | memory_[at + 1u];
        if ((message & kBroadcast) != 0u) {
            fatal.Die("Tps65950Power: warm-reset sequence word 0x%02X holds broadcast message 0x%04X; not modelled",
                      word, message);
        }
        const uint32_t resource = (message >> 4) & 0xFFu;
        const uint32_t state    = message & 0xFu;
        size_t rail = 0;
        while (rail < 3u && kRails[rail].resource != resource) ++rail;
        const bool    triton  = resource == kTritonReset;
        const bool    clock   = resource == k32kClkOut;
        const bool    known   = triton || clock || rail < 3u;
        const uint8_t dev_grp = triton ? kTritonResetDevGrp : clock ? kClkOut32kDevGrp : rail < 3u ? kRails[rail].dev_grp : 0u;
        const bool    acts    = known && InGroups(dev_grp, groups);
        const bool    triton_step = triton && (state == kStateOff || state == kStateActive);
        const bool    rail_step   = rail < 3u && state == kStateWarmReset;
        const bool    clock_step  = clock && state == kStateActive;
        if (!known || (acts && !triton_step && !rail_step && !clock_step)) {
            fatal.Die("Tps65950Power: warm-reset sequence word 0x%02X sets resource %u to state 0x%X; not modelled", word,
                      resource, state);
        }
        if (acts && triton_step) {
            toggled = toggled || (off && state == kStateActive);
            off     = state == kStateOff;
        }
        if (acts && rail_step) WarmResetRail(rail);
        word = static_cast<uint8_t>(memory_[at + 3u] & 0x3Fu);
    }
    if (off) fatal.Die("Tps65950Power: the warm-reset sequence leaves TRITON_RESET off; not modelled");
    return toggled;
}

void Tps65950PowerMaster::OnReset(bool power_on) {
    if (power_on) {
        LOG(Periph, "[TPS65950] power-on reset\n");
        twl_->LoadPowerOn(false);
        PowerOn();
        return;
    }
    const bool toggled = RunWarmSequence();
    LOG(Periph, "[TPS65950] warm reset: SEQ_ADD_WARM 0x%02X, TRITON_RESET toggled %d\n", twl_->PowerReg(kSeqAddWarm),
        toggled ? 1 : 0);
    if (!toggled) return;
    ResetPowerBits();
    twl_->ResetDigital();
}

void Tps65950PowerMaster::Save(StateWriter& w) {
    w.WriteBytes("tps_pm_memory", memory_, sizeof(memory_));
    w.WriteBytes("tps_pm_known", known_, sizeof(known_));
    w.Write<uint8_t>("tps_pm_key_cfg", key_cfg_ ? 1u : 0u);
    w.Write<uint8_t>("tps_pm_key_tst", key_tst_ ? 1u : 0u);
    w.Write<uint8_t>("tps_pm_key_armed", key_armed_);
}

void Tps65950PowerMaster::Restore(StateReader& r) {
    r.ReadBytes("tps_pm_memory", memory_, sizeof(memory_));
    r.ReadBytes("tps_pm_known", known_, sizeof(known_));
    uint8_t key_cfg = 0, key_tst = 0;
    r.Read("tps_pm_key_cfg", key_cfg);
    r.Read("tps_pm_key_tst", key_tst);
    r.Read("tps_pm_key_armed", key_armed_);
    key_cfg_ = key_cfg != 0u;
    key_tst_ = key_tst != 0u;
}

REGISTER_SERVICE(Tps65950PowerMaster);
