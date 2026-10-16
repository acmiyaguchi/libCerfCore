#include "../page_table_builder.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "siemens_simatic_tp177b_4_family_id.h"

#include <cstdint>
#include <vector>

/* Siemens P177 BSP OEMAddressTable, read verbatim from nk.exe's
   g_oalAddressTable at VA 0x83008C58 (walked by startup MMU setup
   sub_83008ED0). */

namespace {

constexpr uint32_t MB(uint32_t mb) { return mb * 0x100000u; }

enum class OatKind { Dram, Flash, Mmio };

struct OatEntry {
    uint32_t va_base;
    uint32_t pa_base;
    uint32_t size;
    OatKind  kind;
};

constexpr OatEntry kOat[] = {
    { 0x80000000u, 0x30000000u, MB(64), OatKind::Dram  }, /* SDRAM primary      */
    { 0x90000000u, 0x34000000u, MB(8),  OatKind::Dram  }, /* SDRAM extended      */
    { 0x84000000u, 0x00000000u, MB(32), OatKind::Flash }, /* nGCS0: NOR boot     */
    { 0x86000000u, 0x08000000u, MB(32), OatKind::Mmio  }, /* nGCS1: ext display  */
    { 0x88000000u, 0x10000000u, MB(32), OatKind::Mmio  }, /* nGCS2               */
    { 0x8A000000u, 0x18000000u, MB(32), OatKind::Mmio  }, /* nGCS3               */
    { 0x8C000000u, 0x20000000u, MB(32), OatKind::Mmio  }, /* nGCS4: CPLD/flash   */
    { 0x8E000000u, 0x28000000u, MB(32), OatKind::Mmio  }, /* nGCS5: ethernet     */
    { 0x90800000u, 0x48000000u, MB(1),  OatKind::Mmio  }, /* Memory controller   */
    { 0x90900000u, 0x49000000u, MB(1),  OatKind::Mmio  }, /* USB host            */
    { 0x90A00000u, 0x4A000000u, MB(1),  OatKind::Mmio  }, /* Interrupt control   */
    { 0x90B00000u, 0x4B000000u, MB(1),  OatKind::Mmio  }, /* DMA control         */
    { 0x90C00000u, 0x4C000000u, MB(1),  OatKind::Mmio  }, /* Clock & power       */
    { 0x90D00000u, 0x4D000000u, MB(1),  OatKind::Mmio  }, /* LCD control         */
    { 0x90E00000u, 0x4E000000u, MB(1),  OatKind::Mmio  }, /* NAND flash control  */
    { 0x91000000u, 0x50000000u, MB(1),  OatKind::Mmio  }, /* UART control        */
    { 0x91100000u, 0x51000000u, MB(1),  OatKind::Mmio  }, /* PWM timer           */
    { 0x91200000u, 0x52000000u, MB(1),  OatKind::Mmio  }, /* USB device          */
    { 0x91300000u, 0x53000000u, MB(1),  OatKind::Mmio  }, /* Watchdog timer      */
    { 0x91400000u, 0x54000000u, MB(1),  OatKind::Mmio  }, /* IIC control         */
    { 0x91500000u, 0x55000000u, MB(1),  OatKind::Mmio  }, /* IIS control         */
    { 0x91600000u, 0x56000000u, MB(1),  OatKind::Mmio  }, /* I/O port            */
    { 0x91700000u, 0x57000000u, MB(1),  OatKind::Mmio  }, /* RTC control         */
    { 0x91800000u, 0x58000000u, MB(1),  OatKind::Mmio  }, /* A/D converter       */
    { 0x91900000u, 0x59000000u, MB(1),  OatKind::Mmio  }, /* SPI                 */
    { 0x91A00000u, 0x5A000000u, MB(1),  OatKind::Mmio  }, /* SD interface        */
};

/* SP the bootloader hands off; the kernel's StartUp resets it (MOV SP,
   #0x300DEFFC) before use. Top of the contiguous SDRAM the OAT maps. */
constexpr uint32_t kInitStackTopPa = 0x34800000u;

class SiemensSimaticTp177b4PageTableBuilder : public PageTableBuilder {
public:
    using PageTableBuilder::PageTableBuilder;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticTp177b4;
    }

    uint32_t InitStackTopPa() const override { return kInitStackTopPa; }

    uint32_t VaToPa(uint32_t va) const override {
        for (const auto& e : kOat) {
            if (va >= e.va_base && va < e.va_base + e.size) {
                return e.pa_base + (va - e.va_base);
            }
        }
        LOG(Caution, "SiemensSimaticTp177b4PageTableBuilder::VaToPa: VA 0x%08X outside "
                     "every P177 OAT band\n", va);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    std::vector<DramRegion> CachedDramRegions() const override {
        std::vector<DramRegion> regions;
        for (const auto& e : kOat) {
            if (e.kind == OatKind::Dram) {
                regions.push_back({ e.va_base, e.pa_base, e.size });
            }
        }
        return regions;
    }

    std::vector<BackedRegion> BackedMemoryRegions() const override {
        std::vector<BackedRegion> regions;
        for (const auto& e : kOat) {
            if (e.kind == OatKind::Mmio) continue;
            const MemoryKind kind =
                (e.kind == OatKind::Flash) ? MemoryKind::kFlash : MemoryKind::kRam;
            regions.push_back({ e.va_base, e.pa_base, e.size, kind });
        }
        return regions;
    }

    std::vector<DramRegion> MappedVaSpans() const override {
        std::vector<DramRegion> regions;
        for (const auto& e : kOat) {
            regions.push_back({ e.va_base, e.pa_base, e.size });
        }
        return regions;
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(SiemensSimaticTp177b4PageTableBuilder, PageTableBuilder);
