#include "siemens_simatic_mp377_mram.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/boards/board_context.h>
#include "../../boards/siemens_simatic_mp377/siemens_simatic_mp377_family_id.h"

namespace siemens_simatic_mp377 {

bool SiemensSimaticMp377Mram::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMp377;
}

void SiemensSimaticMp377Mram::OnReady() {
    ResetErased();
    SeedBspioBootState();
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint32_t SiemensSimaticMp377Mram::MmioBase() const {
    return kMp377MramBase;
}
uint32_t SiemensSimaticMp377Mram::MmioSize() const {
    return kMp377MramSize;
}

uint8_t SiemensSimaticMp377Mram::ReadByte(uint32_t addr) {
    return mram_[addr - MmioBase()];
}

uint16_t SiemensSimaticMp377Mram::ReadHalf(uint32_t addr) {
    return cerf::le::U16(mram_.data(), addr - MmioBase());
}

uint32_t SiemensSimaticMp377Mram::ReadWord(uint32_t addr) {
    return cerf::le::U32(mram_.data(), addr - MmioBase());
}

void SiemensSimaticMp377Mram::WriteByte(uint32_t addr, uint8_t value) {
    mram_[addr - MmioBase()] = value;
}

void SiemensSimaticMp377Mram::WriteHalf(uint32_t addr, uint16_t value) {
    cerf::le::Put16(mram_.data() + (addr - MmioBase()), value);
}

void SiemensSimaticMp377Mram::WriteWord(uint32_t addr, uint32_t value) {
    cerf::le::Put32(mram_.data() + (addr - MmioBase()), value);
}

void SiemensSimaticMp377Mram::SaveState(StateWriter& w) {
    w.WriteBytes("mram", mram_.data(), mram_.size());
}

void SiemensSimaticMp377Mram::RestoreState(StateReader& r) {
    r.ReadBytes("mram", mram_.data(), mram_.size());
}

uint8_t SiemensSimaticMp377Mram::ReadAliasByte(uint32_t alias_pa) const {
    return mram_[OffsetFromAlias(alias_pa)];
}

void SiemensSimaticMp377Mram::WriteAliasByte(uint32_t alias_pa, uint8_t value) {
    mram_[OffsetFromAlias(alias_pa)] = value;
}

void SiemensSimaticMp377Mram::SeedBspioBootState() {
    cerf::le::Put32(mram_.data() + kMp377BspioBootStateOffset, kMp377BspioBootStateUpdateOnce);
}

void SiemensSimaticMp377Mram::ResetErased() {
    mram_.fill(0xFFu);
}

uint32_t SiemensSimaticMp377Mram::OffsetFromAlias(uint32_t alias_pa) const {
    return alias_pa - kMp377MramAliasPa;
}

REGISTER_SERVICE(SiemensSimaticMp377Mram);

} // namespace siemens_mp377
