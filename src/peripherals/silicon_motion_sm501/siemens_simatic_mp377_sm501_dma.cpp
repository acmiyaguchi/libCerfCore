#include "siemens_simatic_mp377_sm501_dma.h"
#include <cerfcore/core/fatal.h>

#include "siemens_simatic_mp377_sm501_fb.h"
#include "siemens_simatic_mp377_sm501_internal.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/siemens_simatic_mp377/siemens_simatic_mp377_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/core/log.h>
#include <cerfcore/socs/guest_cpu_reset.h>

namespace siemens_simatic_mp377 {

bool SiemensSimaticMp377Sm501Dma::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && board->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMp377;
}

void SiemensSimaticMp377Sm501Dma::OnReady() {
    registers_ = &emu_.Get<SiemensSimaticMp377Sm501Regs>();
    memory_ = &emu_.Get<EmulatedMemory>();
    fb_ = &emu_.Get<SiemensSimaticMp377Sm501Fb>();
    mcu_ = &emu_.Get<SiemensSimaticMp377Sm501AudioMcu>();
    Reset();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
        Reset();
    });
}

bool SiemensSimaticMp377Sm501Dma::IsRegister(uint32_t offset) {
    return offset == kSdramAddressReg || offset == kSramAddressReg || offset == kSizeControlReg ||
           offset == kDma1SourceReg || offset == kDma1DestinationReg || offset == kDma1SizeControlReg ||
           offset == kAbortInterruptReg;
}

void SiemensSimaticMp377Sm501Dma::Reset() {
    auto& registers = Registers();
    registers.regs_[kSdramAddressReg / 4u] = 0u;
    registers.regs_[kSramAddressReg / 4u] = 0u;
    registers.regs_[kSizeControlReg / 4u] = 0u;
    registers.regs_[kDma1SourceReg / 4u] = 0u;
    registers.regs_[kDma1DestinationReg / 4u] = 0u;
    registers.regs_[kDma1SizeControlReg / 4u] = 0u;
    registers.regs_[kAbortInterruptReg / 4u] = 0u;
    registers.ClearSm501InterruptBits(kSm501DmaIrqBit);
}

uint32_t SiemensSimaticMp377Sm501Dma::Read(uint32_t offset) const {
    auto& registers = Registers();
    switch (offset) {
    case kSdramAddressReg:
        return registers.regs_[kSdramAddressReg / 4u] & (kSdramExternalBit | kSdramChipSelectBit | kSdramAddressMask);
    case kSramAddressReg: return registers.regs_[kSramAddressReg / 4u] & kSramAddressMask;
    case kSizeControlReg: return registers.regs_[kSizeControlReg / 4u] & (kActBit | kDirectionBit | kSizeMask);
    case kDma1SourceReg:
    case kDma1DestinationReg:
        return registers.regs_[offset / 4u] & (kSdramExternalBit | kSdramChipSelectBit | kSdramAddressMask);
    case kDma1SizeControlReg: return registers.regs_[offset / 4u] & (kActBit | kDma1SizeMask);
    case kAbortInterruptReg:
        return registers.regs_[kAbortInterruptReg / 4u] & (kInterruptChannel0Bit | kInterruptChannel1Bit);
    default: Unsupported("read", offset, 0u);
    }
}

void SiemensSimaticMp377Sm501Dma::Write(uint32_t offset, uint32_t value) {
    auto& registers = Registers();
    switch (offset) {
    case kSdramAddressReg:
        registers.regs_[offset / 4u] = value & (kSdramExternalBit | kSdramChipSelectBit | kSdramAddressMask);
        return;
    case kSramAddressReg: registers.regs_[offset / 4u] = value & kSramAddressMask; return;
    case kSizeControlReg:
        registers.regs_[offset / 4u] = value & (kActBit | kDirectionBit | kSizeMask);
        if ((value & kActBit) != 0u) ExecuteTransfer();
        return;
    case kDma1SourceReg:
    case kDma1DestinationReg:
        registers.regs_[offset / 4u] = value & (kSdramExternalBit | kSdramChipSelectBit | kSdramAddressMask);
        return;
    case kDma1SizeControlReg:
        registers.regs_[offset / 4u] = value & (kActBit | kDma1SizeMask);
        if ((value & kActBit) != 0u) ExecuteDma1();
        return;
    case kAbortInterruptReg: {
        uint32_t irq = registers.regs_[kAbortInterruptReg / 4u] &
                       (kInterruptChannel0Bit | kInterruptChannel1Bit);
        if ((value & kAbortChannel0Bit) != 0u) {
            AbortTransfer();
            irq &= ~kInterruptChannel0Bit;
        }
        if ((value & kAbortChannel1Bit) != 0u) {
            AbortDma1();
            irq &= ~kInterruptChannel1Bit;
        }
        /* SM501 Databook 13-5: Int[0] is cleared by writing zero. */
        if ((value & kInterruptChannel0Bit) == 0u) irq &= ~kInterruptChannel0Bit;
        if ((value & kInterruptChannel1Bit) == 0u) irq &= ~kInterruptChannel1Bit;
        if (irq == 0u) {
            registers.ClearSm501InterruptBits(kSm501DmaIrqBit);
        }
        registers.regs_[kAbortInterruptReg / 4u] = irq;
        return;
    }
    default: Unsupported("write", offset, value);
    }
}

[[noreturn]] void SiemensSimaticMp377Sm501Dma::Unsupported(const char* operation, uint32_t offset, uint32_t value) const {
    emu_.Get<Fatal>().Die("MP377 SM501 DMA unsupported %s offset=0x%08X value=0x%08X", operation, offset, value);
}

bool SiemensSimaticMp377Sm501Dma::DecodeSramOffset(uint32_t& offset, uint32_t size) {
    auto& registers = Registers();
    offset = registers.regs_[kSramAddressReg / 4u] & kSramAddressMask;
    const uint32_t sram_size = SiemensSimaticMp377Sm501AudioMcu::kSramLimit - SiemensSimaticMp377Sm501AudioMcu::kSramBase;
    if (size != 0u && (offset >= sram_size || size > sram_size || offset + size > sram_size)) {
        registers.HaltUnsupportedAccess("SM501 DMA0 SRAM range outside 8051 SRAM", kSm501RegsBarPa + kSramAddressReg,
                                        offset + size);
    }
    return true;
}

bool SiemensSimaticMp377Sm501Dma::ReadMemoryByte(uint32_t address, uint8_t& value) {
    auto& registers = Registers();
    const bool external = (address & kSdramExternalBit) != 0u;
    const uint32_t local_address = address & 0x03FFFFFFu;
    if (external) {
        const uint32_t physical_address = local_address +
            ((address & kSdramChipSelectBit) != 0u ? 0x04000000u : 0u);
        const uint8_t* host = memory_->TryTranslate(physical_address);
        if (!host)
            registers.HaltUnsupportedAccess("SM501 DMA system-memory read outside RAM", physical_address, address);
        value = *host;
        return true;
    }
    if (local_address >= kSm501FbBytes) {
        registers.HaltUnsupportedAccess("SM501 DMA0 local memory read outside BAR0", kSm501FbBarPa + local_address,
                                        address);
    }
    value = fb_->Vram()[local_address];
    return true;
}

bool SiemensSimaticMp377Sm501Dma::WriteMemoryByte(uint32_t address, uint8_t value) {
    auto& registers = Registers();
    const bool external = (address & kSdramExternalBit) != 0u;
    const uint32_t local_address = address & 0x03FFFFFFu;
    if (external) {
        const uint32_t physical_address = local_address +
            ((address & kSdramChipSelectBit) != 0u ? 0x04000000u : 0u);
        uint8_t* host = memory_->TryTranslateWrite(physical_address);
        if (!host)
            registers.HaltUnsupportedAccess("SM501 DMA system-memory write outside RAM", physical_address, address);
        *host = value;
        return true;
    }
    if (local_address >= kSm501FbBytes) {
        registers.HaltUnsupportedAccess("SM501 DMA0 local memory write outside BAR0", kSm501FbBarPa + local_address,
                                        address);
    }
    fb_->MutableVramFor2d()[local_address] = value;
    fb_->Note2dWrite(local_address, 1u);
    return true;
}

void SiemensSimaticMp377Sm501Dma::ExecuteTransfer() {
    auto& registers = Registers();
    const uint32_t control = registers.regs_[kSizeControlReg / 4u] & (kActBit | kDirectionBit | kSizeMask);
    const uint32_t size = control & kSizeMask;
    uint32_t sram_relative = 0u;
    DecodeSramOffset(sram_relative, size);

    const uint32_t address_register =
        registers.regs_[kSdramAddressReg / 4u] & (kSdramExternalBit | kSdramChipSelectBit | kSdramAddressMask);
    const uint32_t flags = address_register & (kSdramExternalBit | kSdramChipSelectBit);
    const uint32_t base = address_register & kSdramAddressMask;
    const bool sram_to_memory = (control & kDirectionBit) != 0u;

    for (uint32_t i = 0u; i < size; ++i) {
        const uint32_t sram_offset = SiemensSimaticMp377Sm501AudioMcu::kSramBase + sram_relative + i;
        const uint32_t address = flags | ((base + i) & 0x03FFFFFFu);
        if (sram_to_memory) {
            WriteMemoryByte(address, mcu_->ReadSramByte(sram_offset));
        } else {
            uint8_t value = 0u;
            ReadMemoryByte(address, value);
            mcu_->WriteSramByte(sram_offset, value);
        }
    }
    CompleteTransfer();
    mcu_->RunMmioSlice(16384u);
}

void SiemensSimaticMp377Sm501Dma::CompleteTransfer() {
    auto& registers = Registers();
    registers.regs_[kSizeControlReg / 4u] &= ~kActBit;
    registers.regs_[kAbortInterruptReg / 4u] |= kInterruptChannel0Bit;
    registers.RaiseSm501InterruptBits(kSm501DmaIrqBit);
}

void SiemensSimaticMp377Sm501Dma::ExecuteDma1() {
    auto& registers = Registers();
    const uint32_t source = registers.regs_[kDma1SourceReg / 4u];
    const uint32_t destination = registers.regs_[kDma1DestinationReg / 4u];
    const uint32_t size = registers.regs_[kDma1SizeControlReg / 4u] & kDma1SizeMask;
    for (uint32_t i = 0u; i < size; ++i) {
        uint8_t value = 0u;
        ReadMemoryByte(source + i, value);
        WriteMemoryByte(destination + i, value);
    }
    CompleteDma1();
}

void SiemensSimaticMp377Sm501Dma::CompleteDma1() {
    auto& registers = Registers();
    registers.regs_[kDma1SizeControlReg / 4u] &= ~kActBit;
    registers.regs_[kAbortInterruptReg / 4u] |= kInterruptChannel1Bit;
    registers.RaiseSm501InterruptBits(kSm501DmaIrqBit);
}

void SiemensSimaticMp377Sm501Dma::AbortTransfer() {
    auto& registers = Registers();
    registers.regs_[kSizeControlReg / 4u] &= ~kActBit;
    registers.regs_[kAbortInterruptReg / 4u] &= ~kInterruptChannel0Bit;
    if ((registers.regs_[kAbortInterruptReg / 4u] & kInterruptChannel1Bit) == 0u)
        registers.ClearSm501InterruptBits(kSm501DmaIrqBit);
}

void SiemensSimaticMp377Sm501Dma::AbortDma1() {
    auto& registers = Registers();
    registers.regs_[kDma1SizeControlReg / 4u] &= ~kActBit;
    registers.regs_[kAbortInterruptReg / 4u] &= ~kInterruptChannel1Bit;
    if ((registers.regs_[kAbortInterruptReg / 4u] & kInterruptChannel0Bit) == 0u)
        registers.ClearSm501InterruptBits(kSm501DmaIrqBit);
}

SiemensSimaticMp377Sm501Regs& SiemensSimaticMp377Sm501Dma::Registers() const {
    return *registers_;
}

REGISTER_SERVICE(SiemensSimaticMp377Sm501Dma);

} // namespace siemens_mp377
