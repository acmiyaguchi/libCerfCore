#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/boards/board_context.h>
#include "casio_jx670_family_id.h"

#include <cstdint>

/* PPFS debug parallel port on External I/O area 1 (IOCS1#, 0x0C000000-0x0DFFFFFF,
   VR4131 UM U15350EJ2V0UM Fig 3-1 p.75), modeled absent. nk_main_kernel.exe probe
   @0x9F034174-0x9F03419C: sh 0x80 -> 0x0C000124 + lhu bit7, sh 0 + lhu bit7; a
   latching port passes the probe into the signature regs 0x0C000120/0x0C000520
   @0x9F0341C0-C8; failure prints "Port Err= %x" / "NoPPFS= TRUE" @0x9F0010D8. */

namespace {

constexpr uint32_t kBase = 0x0C000120u;
constexpr uint32_t kSize = 0x8u;
constexpr uint32_t kOffData = 0x4u;

class CasioJx670PpfsDebugPort : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::CasioJx670;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint16_t ReadHalf(uint32_t addr) override {
        if (addr - kBase == kOffData) return 0u;
        return Peripheral::ReadHalf(addr);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        /* The probe's two known writes (sh 0x80 @0x9F034178, sh 0 @0x9F034190). */
        if (addr - kBase == kOffData && (value == 0x80u || value == 0u)) return;
        Peripheral::WriteHalf(addr, value);
    }
};

}  /* namespace */

REGISTER_SERVICE(CasioJx670PpfsDebugPort);
