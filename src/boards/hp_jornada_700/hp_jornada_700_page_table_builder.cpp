#include "../page_table_builder.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "hp_jornada_700_family_id.h"

#include <cstdint>
#include <vector>

namespace {

constexpr uint32_t MB(uint32_t mb) { return mb * 0x100000u; }

enum class OatKind { Dram, Flash, Mmio };

struct OatEntry {
    uint32_t va_base;
    uint32_t pa_base;
    uint32_t size;
    OatKind  kind;
};

/* OEMAddressTable from the HP "Comanche" firmware in nk.exe (IDA
   0x80001348, consumed by section-builder sub_8005541C as (VA,PA,MB)
   triplets). The firmware's 48 MB DRAM-extension row at PA 0xC1000000 is
   split into populated 16 MB (Dram) + unpopulated 32 MB (Mmio): the 720
   has 32 MB total, and one Dram row would back 64 MB and make
   OEMGetExtensionDRAM report the wrong size. VaToPa is unchanged. */
constexpr OatEntry kOat[] = {
    { 0x88000000u, 0xC0000000u, MB(16), OatKind::Dram  }, /* DRAM bank, low 16 MB */
    { 0x89000000u, 0xC1000000u, MB(16), OatKind::Dram  }, /* DRAM extension, populated 16 MB (firmware row is 48 MB) */
    { 0x8A000000u, 0xC2000000u, MB(32), OatKind::Mmio  }, /* DRAM extension, unpopulated remainder of firmware's 48 MB row */
    { 0x80000000u, 0x00000000u, MB(32), OatKind::Flash }, /* System Flash/ROM */
    { 0x82000000u, 0x48000000u, MB( 4), OatKind::Mmio  }, /* EPSON SED1356 registers */
    { 0x82400000u, 0x48200000u, MB( 4), OatKind::Mmio  }, /* EPSON SED1356 frame buffer */
    { 0x82800000u, 0x08000000u, MB( 4), OatKind::Mmio  }, /* static bank 1 */
    { 0x82C00000u, 0x40000000u, MB( 4), OatKind::Mmio  }, /* SA-1111 companion-chip registers */
    { 0x83000000u, 0x80000000u, MB( 4), OatKind::Mmio  }, /* SA-1110 system control (PPC/UART/GPIO/OST) */
    { 0x83400000u, 0x90000000u, MB( 4), OatKind::Mmio  }, /* SA-1110 power manager / RTC / reset */
    { 0x83800000u, 0xA0000000u, MB( 4), OatKind::Mmio  }, /* SA-1110 memory controller */
    { 0x83C00000u, 0xB0000000u, MB( 4), OatKind::Mmio  }, /* SA-1110 LCD / DMA controller */
    { 0x84000000u, 0x18000000u, MB( 4), OatKind::Mmio  }, /* static bank 3 */
    { 0x84400000u, 0x1A000000u, MB( 4), OatKind::Mmio  }, /* debug CL-CD1284 registers */
    { 0x84800000u, 0x13C00000u, MB( 4), OatKind::Mmio  }, /* static bank 5 alias */
    { 0x85000000u, 0xE0000000u, MB(16), OatKind::Mmio  }, /* SA-1110 zeros bank / cache-flush stripe */
    { 0x90000000u, 0x20000000u, MB(32), OatKind::Mmio  }, /* PCMCIA socket window 0 */
    { 0x92000000u, 0x24000000u, MB(32), OatKind::Mmio  }, /* PCMCIA socket window 1 */
    { 0x94000000u, 0x28000000u, MB(32), OatKind::Mmio  }, /* PCMCIA socket window 2 */
    { 0x96000000u, 0x2C000000u, MB(32), OatKind::Mmio  }, /* PCMCIA socket window 3 */
    { 0x98000000u, 0x30000000u, MB(32), OatKind::Mmio  }, /* PCMCIA socket window 4 */
    { 0x9A000000u, 0x34000000u, MB(32), OatKind::Mmio  }, /* PCMCIA socket window 5 */
    { 0x9C000000u, 0x38000000u, MB(32), OatKind::Mmio  }, /* PCMCIA socket window 6 */
    { 0x9E000000u, 0x3C000000u, MB(32), OatKind::Mmio  }, /* PCMCIA socket window 7 */
};

constexpr uint32_t kDramPaBase     = 0xC0000000u;
constexpr uint32_t kDramSize       = MB(32);
constexpr uint32_t kInitStackTopPa = kDramPaBase + kDramSize;

class HpJornada700PageTableBuilder : public PageTableBuilder {
public:
    using PageTableBuilder::PageTableBuilder;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
    }

    uint32_t InitStackTopPa() const override { return kInitStackTopPa; }
    uint32_t VaToPa(uint32_t va) const override;
    std::vector<DramRegion>   CachedDramRegions()   const override;
    std::vector<BackedRegion> BackedMemoryRegions() const override;
    std::vector<DramRegion>   MappedVaSpans()       const override;
};

uint32_t HpJornada700PageTableBuilder::VaToPa(uint32_t va) const {
    for (const auto& e : kOat) {
        if (va >= e.va_base && va < e.va_base + e.size) {
            return e.pa_base + (va - e.va_base);
        }
    }
    LOG(Caution, "HpJornada700PageTableBuilder::VaToPa: VA 0x%08X outside "
            "every OAT band (nk.exe firmware table @ 0x80001348)\n", va);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

std::vector<DramRegion> HpJornada700PageTableBuilder::CachedDramRegions() const {
    std::vector<DramRegion> regions;
    for (const auto& e : kOat) {
        if (e.kind == OatKind::Dram) {
            regions.push_back({ e.va_base, e.pa_base, e.size });
        }
    }
    return regions;
}

std::vector<BackedRegion>
HpJornada700PageTableBuilder::BackedMemoryRegions() const {
    std::vector<BackedRegion> regions;
    for (const auto& e : kOat) {
        if (e.kind == OatKind::Mmio) continue;
        const MemoryKind kind =
            (e.kind == OatKind::Flash) ? MemoryKind::kFlash : MemoryKind::kRam;
        regions.push_back({ e.va_base, e.pa_base, e.size, kind });
    }
    return regions;
}

std::vector<DramRegion> HpJornada700PageTableBuilder::MappedVaSpans() const {
    std::vector<DramRegion> regions;
    for (const auto& e : kOat) {
        regions.push_back({ e.va_base, e.pa_base, e.size });
    }
    return regions;
}

}  /* namespace */

REGISTER_SERVICE_AS(HpJornada700PageTableBuilder, PageTableBuilder);
