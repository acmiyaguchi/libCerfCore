#include "siemens_simatic_mp377_debug_led_peripheral.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/boards/board_context.h>
#include "siemens_simatic_mp377_family_id.h"

bool SiemensSimaticMp377DebugLedPeripheral::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMp377;
}

void SiemensSimaticMp377DebugLedPeripheral::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint8_t SiemensSimaticMp377DebugLedPeripheral::ReadByte(uint32_t addr) {
    HaltUnsupportedAccess("MP377 debug LED byte read", addr, 0);
}

uint16_t SiemensSimaticMp377DebugLedPeripheral::ReadHalf(uint32_t addr) {
    HaltUnsupportedAccess("MP377 debug LED halfword read", addr, 0);
}

uint32_t SiemensSimaticMp377DebugLedPeripheral::ReadWord(uint32_t addr) {
    HaltUnsupportedAccess("MP377 debug LED word read", addr, 0);
}

void SiemensSimaticMp377DebugLedPeripheral::WriteByte(uint32_t addr, uint8_t value) {
    HaltUnsupportedAccess("MP377 debug LED byte write", addr, value);
}

void SiemensSimaticMp377DebugLedPeripheral::WriteHalf(uint32_t addr, uint16_t value) {
    HaltUnsupportedAccess("MP377 debug LED halfword write", addr, value);
}

void SiemensSimaticMp377DebugLedPeripheral::WriteWord(uint32_t addr, uint32_t value) {
    HaltUnsupportedAccess("MP377 debug LED word write", addr, value);
}
