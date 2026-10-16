#include "sa1111_unit.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../peripheral_dispatcher.h"
#include "sa1111_reset_line.h"
#include "sa1111_sbi.h"
#include "sa1111_system_controller.h"

void Sa1111Unit::OnReady() {
    auto& line  = emu_.Get<Sa1111ResetLine>();
    reset_line_ = &line;
    OnUnitReady();
    emu_.Get<PeripheralDispatcher>().Register(this);
    line.RegisterListener([this](bool held) { OnChipReset(held); });
}

bool Sa1111Unit::ChipHeld() const { return reset_line_->Held(); }

/* SA-1111 Developer's Manual §2.4.2 (page 2-5): "RCLK must be turned on for any register accesses
   to functional blocks beyond the SBI"; §2.3 (page 2-4): "When nRESET is asserted, all on-chip
   activity halts". */
bool Sa1111Unit::Admit(uint32_t addr) {
    if (sbi_ == nullptr) {
        sbi_        = &emu_.Get<Sa1111Sbi>();
        controller_ = &emu_.Get<Sa1111SystemController>();
    }
    sbi_->WakeOnAccess();
    if (!NeedsRclk(addr)) return true;
    return !ChipHeld() && controller_->RclkRunning();
}

uint8_t  Sa1111Unit::ReadByte (uint32_t addr) { return Admit(addr) ? UnitReadByte(addr) : 0u; }
uint16_t Sa1111Unit::ReadHalf (uint32_t addr) { return Admit(addr) ? UnitReadHalf(addr) : 0u; }
uint32_t Sa1111Unit::ReadWord (uint32_t addr) { return Admit(addr) ? UnitReadWord(addr) : 0u; }
uint64_t Sa1111Unit::ReadDword(uint32_t addr) { return Admit(addr) ? UnitReadDword(addr) : 0u; }
void Sa1111Unit::WriteByte (uint32_t addr, uint8_t  value) { if (Admit(addr)) UnitWriteByte(addr, value); }
void Sa1111Unit::WriteHalf (uint32_t addr, uint16_t value) { if (Admit(addr)) UnitWriteHalf(addr, value); }
void Sa1111Unit::WriteWord (uint32_t addr, uint32_t value) { if (Admit(addr)) UnitWriteWord(addr, value); }
void Sa1111Unit::WriteDword(uint32_t addr, uint64_t value) { if (Admit(addr)) UnitWriteDword(addr, value); }

Peripheral::FastReadFn  Sa1111Unit::FastReader() { return Peripheral::FastReader(); }
Peripheral::FastWriteFn Sa1111Unit::FastWriter() { return Peripheral::FastWriter(); }

uint8_t  Sa1111Unit::UnitReadByte (uint32_t addr) { HaltUnsupportedAccess("ReadByte",  addr, 0); }
uint16_t Sa1111Unit::UnitReadHalf (uint32_t addr) { HaltUnsupportedAccess("ReadHalf",  addr, 0); }
uint32_t Sa1111Unit::UnitReadWord (uint32_t addr) { HaltUnsupportedAccess("ReadWord",  addr, 0); }
uint64_t Sa1111Unit::UnitReadDword(uint32_t addr) { HaltUnsupportedAccess("ReadDword", addr, 0); }
void Sa1111Unit::UnitWriteByte (uint32_t addr, uint8_t  value) { HaltUnsupportedAccess("WriteByte",  addr, value); }
void Sa1111Unit::UnitWriteHalf (uint32_t addr, uint16_t value) { HaltUnsupportedAccess("WriteHalf",  addr, value); }
void Sa1111Unit::UnitWriteWord (uint32_t addr, uint32_t value) { HaltUnsupportedAccess("WriteWord",  addr, value); }
void Sa1111Unit::UnitWriteDword(uint32_t addr, uint64_t value) { HaltUnsupportedAccess("WriteDword", addr, value); }
