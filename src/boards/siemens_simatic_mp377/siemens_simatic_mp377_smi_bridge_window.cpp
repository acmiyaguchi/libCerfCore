#include "siemens_simatic_mp377_smi_bridge_window.h"

#include "siemens_simatic_mp377_smi_bridge.h"

#include <cerfcore/boards/board_context.h>
#include "siemens_simatic_mp377_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"

namespace cerfcore {

namespace siemens_simatic_mp377 {

bool SiemensSimaticMp377SmiBridgeWindow::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMp377;
}

void SiemensSimaticMp377SmiBridgeWindow::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint8_t SiemensSimaticMp377SmiBridgeWindow::ReadByte(uint32_t a) {
    return static_cast<uint8_t>(ReadWord(a & ~3u) >> ((a & 3u) * 8u));
}

uint16_t SiemensSimaticMp377SmiBridgeWindow::ReadHalf(uint32_t a) {
    return static_cast<uint16_t>(ReadWord(a & ~3u) >> ((a & 2u) * 8u));
}

uint32_t SiemensSimaticMp377SmiBridgeWindow::ReadWord(uint32_t a) {
    const uint32_t rel = RelativeOffset(a);
    if (!IsSupportedOffset(rel)) {
        HaltUnsupportedAccess("MP377 SMI bridge unknown register read", a, 0);
    }
    return emu_.Get<SiemensSimaticMp377SmiBridge>().Read(rel);
}

void SiemensSimaticMp377SmiBridgeWindow::WriteByte(uint32_t a, uint8_t v) {
    uint32_t w = ReadWord(a & ~3u);
    const uint32_t shift = (a & 3u) * 8u;
    w = (w & ~(0xFFu << shift)) | (static_cast<uint32_t>(v) << shift);
    WriteWord(a & ~3u, w);
}

void SiemensSimaticMp377SmiBridgeWindow::WriteHalf(uint32_t a, uint16_t v) {
    uint32_t w = ReadWord(a & ~3u);
    const uint32_t shift = (a & 2u) * 8u;
    w = (w & ~(0xFFFFu << shift)) | (static_cast<uint32_t>(v) << shift);
    WriteWord(a & ~3u, w);
}

void SiemensSimaticMp377SmiBridgeWindow::WriteWord(uint32_t a, uint32_t v) {
    const uint32_t rel = RelativeOffset(a);
    if (!IsSupportedOffset(rel)) {
        HaltUnsupportedAccess("MP377 SMI bridge unknown register write", a, v);
    }
    emu_.Get<SiemensSimaticMp377SmiBridge>().Write(rel, v);
}

uint32_t SiemensSimaticMp377SmiBridgeWindow::RelativeOffset(uint32_t a) const {
    return (a - MmioBase()) & ~3u;
}

bool SiemensSimaticMp377SmiBridgeWindow::IsSupportedOffset(uint32_t rel) {
    return rel == 0x04u || rel == 0x08u;
}

} // namespace siemens_mp377

}
