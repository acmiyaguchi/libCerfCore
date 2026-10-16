#include "casio_tomcat_lcd.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../lcd/display_size_latch.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../socs/vr41xx/vr41xx_giu.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/boards/board_context.h>
#include "casio_tomcat_family_id.h"

namespace {

/* casio_cassiopeia_e55 nk.exe @0x9E816B40-@0x9E816B4C load 0xF4 0x84 0xB0 0xD0, stored twice
   by @0x9E816B70-@0x9E816BA4. */
constexpr uint8_t kCtrlSeq84[8] = {0xF4u, 0x84u, 0xB0u, 0xD0u, 0xF4u, 0x84u, 0xB0u, 0xD0u};

/* casio_cassiopeia_e55 nk.exe sub_9E814C70 @0x9E814C70-@0x9E814CE8 and ddi.dll sub_14F0DE0
   @0x14F0E74-@0x14F0F04 both store 0xF4 0xC4 0xB0 0xD0 twice. */
constexpr uint8_t kCtrlSeqC4[8] = {0xF4u, 0xC4u, 0xB0u, 0xD0u, 0xF4u, 0xC4u, 0xB0u, 0xD0u};

}  /* namespace */

bool CasioTomcatLcd::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::CasioTomcat;
}

void CasioTomcatLcd::OnReady() {
    fb_.assign(kFbSize, 0u);
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint8_t CasioTomcatLcd::ReadByte(uint32_t addr) {
    const uint32_t off = addr - kBase;
    if (InFb(off)) return fb_[off];
    HaltUnsupportedAccess("ReadByte", addr, 0);
}

uint16_t CasioTomcatLcd::ReadHalf(uint32_t addr) {
    const uint32_t off = addr - kBase;
    if (InFb(off) && off + 1u < kFbSize) return cerf::le::U16(fb_.data(), off);
    HaltUnsupportedAccess("ReadHalf", addr, 0);
}

uint32_t CasioTomcatLcd::ReadWord(uint32_t addr) {
    const uint32_t off = addr - kBase;
    if (InFb(off) && off + 3u < kFbSize) return cerf::le::U32(fb_.data(), off);
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

void CasioTomcatLcd::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off = addr - kBase;
    if (InFb(off)) {
        fb_[off] = value;
        return;
    }
    if (InCtrl(off)) {
        const uint32_t i     = off - kCtrlOffset;
        const uint8_t  guest = static_cast<uint8_t>(~value);
        if (guest != kCtrlSeq84[i] && guest != kCtrlSeqC4[i]) {
            HaltUnsupportedAccess("WriteByte", addr, value);
        }
        ctrl_[i] = value;
        return;
    }
    HaltUnsupportedAccess("WriteByte", addr, value);
}

void CasioTomcatLcd::WriteHalf(uint32_t addr, uint16_t value) {
    const uint32_t off = addr - kBase;
    if (InFb(off) && off + 1u < kFbSize) {
        cerf::le::Put16(fb_.data() + off, value);
        return;
    }
    HaltUnsupportedAccess("WriteHalf", addr, value);
}

void CasioTomcatLcd::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - kBase;
    if (InFb(off) && off + 3u < kFbSize) {
        cerf::le::Put32(fb_.data() + off, value);
        return;
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

bool CasioTomcatLcd::IsDisplayEnabled() {
    return emu_.Get<Vr41xxGiu>().GetPinLevel(kPanelEnableGpio);
}

void CasioTomcatLcd::MaybePublishDisplaySize() {
    size_latch_.PublishOnce(emu_, IsDisplayEnabled());
}

void CasioTomcatLcd::SaveState(StateWriter& w) {
    for (uint32_t i = 0; i < kCtrlCount; ++i) w.Write("ctrl_pin", ctrl_[i]);
    size_latch_.SaveState(w);
    w.WriteBytes("fb_pin", fb_.data(), fb_.size());
}

void CasioTomcatLcd::RestoreState(StateReader& r) {
    for (uint32_t i = 0; i < kCtrlCount; ++i) r.Read("ctrl_pin", ctrl_[i]);
    size_latch_.RestoreState(r);
    r.ReadBytes("fb_pin", fb_.data(), fb_.size());
}

REGISTER_SERVICE(CasioTomcatLcd);
