#include <cerfcore/boards/board_context.h>
#include "symbol_hamptons_display.h"
#include "symbol_hamptons_family_id.h"

#include <cerfcore/boot/guest_cold_boot.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/core/service.h>
#include <cerfcore/cpu/emulated_memory.h>

#include <cstdint>

namespace {

/* nk.exe 0x801BB0D4: NKCreateStaticMapping(0x00A01380, 0x1000), arg0 = PA >> 8. */
constexpr uint32_t kArgsPa = 0xA0138000u;

/* nk.exe 0x801BB864 reads args+0x814; dump.bin OEMAddressTable VA 0x80000000 -> PA 0. */
constexpr uint32_t kFlashKernelVaOff = 0x814u;
constexpr uint32_t kFlashKernelVa    = 0x80000000u;

/* nk.exe 0x801BB8EC reads args+0x888; dump.bin PA 0x00080000 partition table, "Config Block" entry 22 at +0x5C0. */
constexpr uint32_t kPartitionTablePaOff = 0x888u;
constexpr uint32_t kPartitionTablePa    = 0x00080000u;

/* nk.exe 0x801BBBCC reads args+0x890 as "DisplayID = %X". */
constexpr uint32_t kDisplayIdOff = 0x890u;

/* symbol_mk500 MK500c50BenOS013014.bin FlashFx.Dll 0x022E9B94 u16 [args+0x824]
   and u16 [args+0x826], matched at 0x022E9BE0 against record+0/+2 of the table
   at 0x01DEB558; symbol_mk500 MK500c50XenMO0152XX.bin 0x000277CF parts table
   stride 0x28 record (0x0089, 0x8803) size 0x04000000 block 0x00040000. */
constexpr uint32_t kFlashIdOff = 0x824u;
constexpr uint32_t kFlashMfr   = 0x0089u;
constexpr uint32_t kFlashDev   = 0x8803u;

class SymbolHamptonsBspArgs : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SymbolHamptons;
    }

    void OnReady() override {
        WriteArgs();
        emu_.Get<GuestColdBoot>().RegisterReplay([this] { WriteArgs(); });
    }

private:
    void WriteArgs() {
        auto& mem = emu_.Get<EmulatedMemory>();
        const uint32_t display_id = emu_.Get<SymbolHamptonsDisplay>().DisplayId();

        mem.WriteWord(kArgsPa + kFlashKernelVaOff,    kFlashKernelVa);
        mem.WriteWord(kArgsPa + kPartitionTablePaOff, kPartitionTablePa);
        mem.WriteWord(kArgsPa + kDisplayIdOff,        display_id);
        mem.WriteWord(kArgsPa + kFlashIdOff,
                      kFlashMfr | (kFlashDev << 16));

        LOG(Board, "SymbolHamptonsBspArgs: args block at PA 0x%08X; flash kernel VA "
                   "0x%08X, partition table PA 0x%08X, DisplayID 0x%X, flash id "
                   "%04X:%04X\n",
            kArgsPa, kFlashKernelVa, kPartitionTablePa, display_id,
            kFlashMfr, kFlashDev);
    }
};

}

REGISTER_SERVICE(SymbolHamptonsBspArgs);
