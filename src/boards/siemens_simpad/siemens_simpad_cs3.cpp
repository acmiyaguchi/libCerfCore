#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "siemens_simpad_family_id.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include "siemens_simpad_cs3_sink.h"

#include <cstdint>

namespace {

/* CS3 latch bit values, from Linux arch/arm/mach-sa1100/include/mach/simpad.h. */
enum Cs3Bit : uint16_t {
    kVcc5vEn      = 0x0001, kVcc3vEn   = 0x0002, kEn1        = 0x0004,
    kEn0          = 0x0008, kDisplayOn = 0x0010, kPcmciaBuffDis = 0x0020,
    kMqReset      = 0x0040, kPcmciaReset = 0x0080, kDectPowerOn = 0x0100,
    kIrdaSd       = 0x0200, kRs232On   = 0x0400, kSdMediaq   = 0x0800,
    kLed2On       = 0x1000, kIrdaMode  = 0x2000, kEnable5v   = 0x4000,
    kResetSimcard = 0x8000,
};

/* CS3 board-control latch (nCS3), 16-bit. Write and read are separate registers
   at this address: writes set this latch (shadowed); reads return a hardware
   status register (PCMCIA sense / charging / lock). The CE boot stub writes the
   latch at 0x1A000000 (nk.exe 0x80081070). */
class SiemensSimpadCs3 : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimpad;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x1A000000u; }
    uint32_t MmioSize() const override { return 0x00000004u; }

    void WriteWord(uint32_t addr, uint32_t value) override { Latch(addr, value); }
    void WriteHalf(uint32_t addr, uint16_t value) override { Latch(addr, value); }

    uint16_t Shadow() const { return shadow_; }

    /* State image: the latch shadow is the entire guest-writable state. */
    void SaveState(StateWriter& w) override {
        w.Write("shadow", shadow_);
    }
    void RestoreState(StateReader& r) override {
        r.Read("shadow", shadow_);
    }

private:
    void Latch(uint32_t addr, uint32_t value) {
        if (addr != MmioBase()) HaltUnsupportedAccess("Write", addr, value);
        shadow_ = static_cast<uint16_t>(value);
        LOG(Board, "SiemensSimpadCs3: latch=0x%04X [%s%s%s%s%s%s%s%s]\n", shadow_,
            (shadow_ & kDisplayOn)   ? "DISPLAY_ON "    : "",
            (shadow_ & kEnable5v)    ? "ENABLE_5V "     : "",
            (shadow_ & kVcc5vEn)     ? "VCC_5V "        : "",
            (shadow_ & kVcc3vEn)     ? "VCC_3V "        : "",
            (shadow_ & kRs232On)     ? "RS232_ON "      : "",
            (shadow_ & kDectPowerOn) ? "DECT_POWER_ON " : "",
            (shadow_ & kLed2On)      ? "LED2_ON "       : "",
            (shadow_ & kResetSimcard)? "RESET_SIMCARD " : "");
        if (auto* sink = emu_.TryGet<SiemensSimpadCs3Sink>())
            sink->OnCs3LatchChanged(shadow_);
    }

    uint16_t shadow_ = 0;
};

}  /* namespace */

REGISTER_SERVICE(SiemensSimpadCs3);
