#include <cerfcore/boards/board_context.h>
#include "samsung_smdk2410_family_id.h"

#include "../../boot/board_boot_placer.h"
#include <cerfcore/boot/guest_cold_boot.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>

#include <cstdint>

namespace {

constexpr uint32_t kStubFlashPa = 0x4u;
constexpr uint32_t kStub[] = {
    0xE5801000u,  /* STR R1,[R0] - REFRESH: SDRAM self-refresh  */
    0xE5823000u,  /* STR R3,[R2] - MISCCR                       */
    0xE5845000u,  /* STR R5,[R4] - CLKCON <- 0x7fff8 (power off) */
    0xEAFFFFFEu,  /* B .                                        */
};

class SamsungSmdk2410PowerOffStub : public BoardBootPlacer {
public:
    using BoardBootPlacer::BoardBootPlacer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SamsungSmdk2410;
    }

    void OnReady() override {
        emu_.Get<GuestColdBoot>().RegisterReplay([this] { WriteStub(); });
    }

    /* RomPlacer calls this after erasing the flash region to 0xFF. */
    void PlaceAfterRom() override { WriteStub(); }

private:
    void WriteStub() {
        emu_.Get<EmulatedMemory>().CopyIn(kStubFlashPa, kStub, sizeof(kStub));
        LOG(Board, "SamsungSmdk2410PowerOffStub: power-off stub planted at flash offset "
            "0x%X (NAND power-off target 0x88000004)\n", kStubFlashPa);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(SamsungSmdk2410PowerOffStub, BoardBootPlacer);
