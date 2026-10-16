#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/pr31x00/pr31x00_io_pins.h"
#include <cerfcore/boards/board_context.h>
#include "philips_velo_1_dram_card_profile.h"
#include "philips_velo_1_family_id.h"

#include <cstdint>
#include <optional>

namespace cerfcore {

namespace {

/* IO[0] M-Module attached, active high. philips_velo_1_ce1 nk.1.exe sub_9FB11194
   ("Checking for M-Module Attached") branches "No M-Module" on (IODIN & 1) != 1, and
   philips_velo_1_ce1 nk.exe sub_9F40F688 gates the Iceberg-ASIC bring-up on the same
   bit. This board fits the M-Module, so its Iceberg answers CS2. */
constexpr uint32_t kMModuleAttached = 1u << 0;

/* IO[6] Miniature Card detect for slot 2, active low, empty (philips_velo_1_ce1
   nk.1.exe sub_9FB1A0C4, sub_9FB0F424 "No Mini Card in Slot N"). */
constexpr uint32_t kMiniCardSlot2 = 1u << 6;

/* IO[5] slot 1 card detect: philips_velo_1_ce1 nk.exe sub_9F42453C @0x9F42453C
   abandons the DRAM card when IODIN & 0x20 is set. */
constexpr uint32_t kMiniCardSlot1 = 1u << 5;

/* IO[1] carries an M-Module socket status line (philips_velo_1_ce1 nk.1.exe
   sub_9FB2BC6C) that only the absent M-Module drives; IO[2] has no reader in the ROM.
   IO[3] is the serial DTR output (philips_velo_1_ce1 nk.exe sub_9F40E5E0 suspend path:
   IO_CTL = 0x10080800, IODIREC = IODOUT = 0x08). */

/* IODIN ORs this level with the one Pr31x00Io drives, so a pin named here can only
   ever read high. Serial DCD on IO[4] is active low and driven through Pr31x00Io by
   the UART wiring, so it is absent from this set. */
class PhilipsVelo1IoPins : public Pr31x00IoPins {
public:
    using Pr31x00IoPins::Pr31x00IoPins;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::PhilipsVelo1;
    }

    void OnReady() override {
        const bool card = FindPhilipsVelo1DramCard(emu_.Get<BoardContext>().GetBoardId());
        io_din_ = kMModuleAttached | kMiniCardSlot2 | (card ? 0u : kMiniCardSlot1);
    }

    uint32_t IoDin() const override { return io_din_; }

    /* The board itself holds no MFIO input high. The only pin the ROM reads is serial
       CTS on MFIODIN<30> (philips_velo_1_ce1 serial.dll sub_1EB1F04, sub_1EB1DE0), and
       the UART wiring drives its level through Pr31x00Io, not here. */
    std::optional<uint32_t> MfioDin() const override { return 0u; }

private:
    uint32_t io_din_ = 0;
};

}  /* namespace */

REGISTER_SERVICE_AS(PhilipsVelo1IoPins, Pr31x00IoPins);

}
