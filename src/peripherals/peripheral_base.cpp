#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

#include <typeinfo>

uint8_t  Peripheral::ReadByte (uint32_t addr) { HaltUnsupportedAccess("ReadByte",  addr, 0); }
uint16_t Peripheral::ReadHalf (uint32_t addr) { HaltUnsupportedAccess("ReadHalf",  addr, 0); }
uint32_t Peripheral::ReadWord (uint32_t addr) { HaltUnsupportedAccess("ReadWord",  addr, 0); }
uint64_t Peripheral::ReadDword(uint32_t addr) { HaltUnsupportedAccess("ReadDword", addr, 0); }
void Peripheral::WriteByte (uint32_t addr, uint8_t  value) { HaltUnsupportedAccess("WriteByte",  addr, value); }
void Peripheral::WriteHalf (uint32_t addr, uint16_t value) { HaltUnsupportedAccess("WriteHalf",  addr, value); }
void Peripheral::WriteWord (uint32_t addr, uint32_t value) { HaltUnsupportedAccess("WriteWord",  addr, value); }
void Peripheral::WriteDword(uint32_t addr, uint64_t value) { HaltUnsupportedAccess("WriteDword", addr, value); }

uint32_t Peripheral::AutoFastRead(void* ctx, uint32_t off, uint32_t width) {
    auto* p = static_cast<Peripheral*>(ctx);
    const uint32_t addr = p->MmioBase() + off;
    switch (width) {
        case 1: return p->ReadByte(addr);
        case 2: return p->ReadHalf(addr);
        case 4: return p->ReadWord(addr);
    }
    p->HaltUnsupportedAccess("AutoFastRead", addr, width);
}

void Peripheral::AutoFastWrite(void* ctx, uint32_t off, uint32_t value, uint32_t width) {
    auto* p = static_cast<Peripheral*>(ctx);
    const uint32_t addr = p->MmioBase() + off;
    switch (width) {
        case 1: p->WriteByte(addr, static_cast<uint8_t> (value)); return;
        case 2: p->WriteHalf(addr, static_cast<uint16_t>(value)); return;
        case 4: p->WriteWord(addr, value); return;
    }
    p->HaltUnsupportedAccess("AutoFastWrite", addr, value);
}

void Peripheral::HaltUnsupportedAccess(const char* op,
                                       uint32_t addr,
                                       uint64_t value) const {
    emu_.Get<Fatal>().Die("Peripheral '%s' rejected %s at 0x%08X (value 0x%016llX)",
                          typeid(*this).name(), op, addr,
                          static_cast<unsigned long long>(value));
}
