#include "siemens_simatic_mp377_board_io_window.h"
#include "siemens_simatic_mp377_family_id.h"

namespace siemens_simatic_mp377_board_io_detail {

bool SiemensSimaticMp377BoardIoWindow::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMp377;
}

void SiemensSimaticMp377BoardIoWindow::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint8_t SiemensSimaticMp377BoardIoWindow::ReadByte(uint32_t addr) {
    HaltUnsupportedAccess("MP377 unsupported board byte read", addr, 0);
}

uint16_t SiemensSimaticMp377BoardIoWindow::ReadHalf(uint32_t addr) {
    HaltUnsupportedAccess("MP377 unsupported board halfword read", addr, 0);
}

uint32_t SiemensSimaticMp377BoardIoWindow::ReadWord(uint32_t addr) {
    HaltUnsupportedAccess("MP377 unsupported board word read", addr, 0);
}

void SiemensSimaticMp377BoardIoWindow::WriteByte(uint32_t addr, uint8_t value) {
    HaltUnsupportedAccess("MP377 unsupported board byte write", addr, value);
}

void SiemensSimaticMp377BoardIoWindow::WriteHalf(uint32_t addr, uint16_t value) {
    HaltUnsupportedAccess("MP377 unsupported board halfword write", addr, value);
}

void SiemensSimaticMp377BoardIoWindow::WriteWord(uint32_t addr, uint32_t value) {
    HaltUnsupportedAccess("MP377 unsupported board word write", addr, value);
}

} // namespace mp377_board_io_detail
