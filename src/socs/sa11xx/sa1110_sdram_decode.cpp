#include "sa1110_sdram_decode.h"

#include "sa1110_id.h"
#include "sa1110_sdram_geometry.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include "../../execution/engine_bridge.h"
#include <cerfengine/arm/arm_mmu.h>

REGISTER_SERVICE(Sa1110SdramDecode);

namespace {

constexpr uint32_t kBank0Base      = 0xC0000000u;
constexpr uint32_t kMdcnfgDe0      = 1u << 0;
constexpr uint32_t kMdcnfgDtim0    = 1u << 2;
constexpr uint32_t kMdcnfgDwid0    = 1u << 3;
constexpr uint32_t kMdcnfgDrac0Pos = 4;
constexpr uint32_t kRowBitsAtDrac0 = 9;

/* SA-1110 Developer's Manual §10.4.1 Table 10-7 (p. 10-30), 32-bit data bus: the internal
   address bit on DRA8 and on DRA9 at CAS time, for 9 through 15 row address bits. */
constexpr uint8_t kCasDra8[7] = {19, 20, 21, 22, 23, 24, 25};
constexpr uint8_t kCasDra9[7] = {19, 21, 22, 23, 24, 25, 26};

}

bool Sa1110SdramDecode::ShouldRegister() {
    return emu_.Get<BoardContext>().GetSocId() == SocId::Sa1110 &&
           emu_.TryGet<Sa1110SdramGeometry>() != nullptr;
}

void Sa1110SdramDecode::OnReady() {
    const auto& geometry = emu_.Get<Sa1110SdramGeometry>();
    const uint32_t rows    = geometry.RowBits();
    const uint32_t columns = geometry.ColumnBits();
    const uint32_t banks   = geometry.BankBits();
    if (banks != 2u || rows < 12u || rows > 13u || columns < 8u || columns > 10u)
        emu_.Get<Fatal>().Die("Sa1110SdramDecode: SDRAM with %u row, %u column and %u bank "
                              "address bits is not modeled", rows, columns, banks);
    const uint32_t native_drac = rows + banks - kRowBitsAtDrac0;
    for (uint32_t drac = 0; drac < decodes_.size(); ++drac)
        decodes_[drac] = Build(rows, columns, drac, native_drac);
}

/* SA-1110 Developer's Manual §10.4.1 Table 10-7 (p. 10-30): DRA 14:0 = IA 24:10 at
   RAS time, DRA13 / DRA12 carry BA0 / BA1, and DRA 7:0 = IA 9:2 at CAS time. */
EmulatedMemory::AddressDecode Sa1110SdramDecode::Build(uint32_t rows, uint32_t columns,
                                                      uint32_t drac, uint32_t native_drac) {
    uint32_t targets[32] = {};
    const auto drive = [&targets](uint32_t source, uint32_t canonical) {
        targets[source] |= 1u << canonical;
    };
    for (uint32_t bit = 0; bit < 10u; ++bit) drive(bit, bit);
    for (uint32_t pin = 0; pin < 12u; ++pin) drive(pin + 10u, pin + 10u);
    if (rows == 13u) drive(24u, 24u);
    drive(22u, 22u);
    drive(23u, 23u);
    if (columns >= 9u)  drive(kCasDra8[drac], kCasDra8[native_drac]);
    if (columns >= 10u) drive(kCasDra9[drac], kCasDra9[native_drac]);

    EmulatedMemory::AddressDecode decode{};
    decode.pass_mask = 0u;
    for (uint32_t bit = 0; bit < 32u; ++bit) {
        if (targets[bit] == 1u << bit) {
            decode.pass_mask |= 1u << bit;
        } else if (targets[bit] != 0u) {
            decode.gather_mask |= 1u << bit;
            decode.targets[bit] = targets[bit];
        }
    }
    return decode;
}

void Sa1110SdramDecode::ApplyMdcnfg(uint32_t mdcnfg) {
    const uint32_t drac = (mdcnfg >> kMdcnfgDrac0Pos) & 7u;
    const EmulatedMemory::AddressDecode* next = nullptr;
    if (mdcnfg & kMdcnfgDe0) {
        if (!(mdcnfg & kMdcnfgDtim0) || (mdcnfg & kMdcnfgDwid0) || drac >= decodes_.size())
            emu_.Get<Fatal>().Die("Sa1110SdramDecode: MDCNFG 0x%08X selects a bank pair 0/1 "
                                  "decode that is not modeled", mdcnfg);
        next = &decodes_[drac];
    }
    if (next == active_) return;
    active_ = next;
    emu_.Get<EmulatedMemory>().SetAddressDecode(kBank0Base, next);
    emu_.Get<EngineBridge>().Engine().Get<cerfengine::ArmMmu>().InvalidateAllTlbs();
    LOG(Mem, "SA-1110 MDCNFG 0x%08X: DE0 %u DRAC0 %u\n", mdcnfg, mdcnfg & kMdcnfgDe0, drac);
}
