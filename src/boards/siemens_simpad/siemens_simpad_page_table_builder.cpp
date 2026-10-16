#include "../page_table_builder.h"

#include "siemens_simpad_boot.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "siemens_simpad_family_id.h"

#include <algorithm>
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

/* SIMpad SL4 OEMAddressTable, decoded from nk.exe at IDA 0x80081400 and
   verified against consumer sub_800A147C (walks it as (VA,PA,sizeMB) triplets
   to a zero terminator). Byte-identical in the CE4.10 and HPC2000 ROMs. PA
   classification per SA-1110 devman Jun-2000 §2.4 / Figure 2-3. */
constexpr OatEntry kOat[] = {
    { 0x80000000u, 0xC1E00000u, MB( 2), OatKind::Dram  }, /* kernel cached window -> DRAM (NK image base maps here) */
    { 0x80200000u, 0x00200000u, MB(14), OatKind::Flash }, /* NOR flash CS0, +2MB (cached) */
    /* CS1 is Flash not Mmio: pTOC=0x81a7a17c (< physlast 0x81a7dc10) puts the
       ROM TOC + module headers in this band - read-only OS-image content. */
    { 0x81000000u, 0x08000000u, MB(16), OatKind::Flash }, /* static bank CS1 (cached) - holds ROM TOC + modules */
    { 0x8C000000u, 0xC0000000u, MB(30), OatKind::Dram  }, /* DRAM bank 0 base (cached) */
    { 0x8DE00000u, 0xC2000000u, MB(32), OatKind::Dram  }, /* DRAM, upper window (cached) */
    { 0x84000000u, 0x00000000u, MB(16), OatKind::Flash }, /* NOR flash CS0 full, uncached view */
    { 0x82000000u, 0x08000000u, MB(16), OatKind::Flash }, /* static bank CS1, uncached view */
    { 0x8BC00000u, 0x1A000000u, MB( 1), OatKind::Mmio  }, /* static bank CS3 alias */
    { 0x8BD00000u, 0x18000000u, MB( 1), OatKind::Mmio  }, /* static bank CS3 */
    { 0x88000000u, 0x80000000u, MB( 4), OatKind::Mmio  }, /* SA-1110 system control (PPC/UART/GPIO/OST) */
    { 0x89000000u, 0x90000000u, MB( 4), OatKind::Mmio  }, /* SA-1110 power manager / RTC / reset */
    { 0x8A000000u, 0xA0000000u, MB( 4), OatKind::Mmio  }, /* SA-1110 memory controller */
    { 0x8B000000u, 0xB0000000u, MB( 4), OatKind::Mmio  }, /* SA-1110 LCD / DMA controller */
    { 0x88C00000u, 0xE0000000u, MB( 4), OatKind::Mmio  }, /* SA-1110 zeros bank / cache-flush stripe */
    { 0x8B800000u, 0x20000000u, MB( 2), OatKind::Mmio  }, /* PCMCIA socket 0 attribute/IO window */
    { 0x8BA00000u, 0x30000000u, MB( 2), OatKind::Mmio  }, /* PCMCIA socket 1 attribute/IO window */
    { 0x94000000u, 0xE0000000u, MB( 4), OatKind::Mmio  }, /* zeros bank alias */
    { 0x90000000u, 0x28000000u, MB( 8), OatKind::Mmio  }, /* PCMCIA socket 0 mid window */
    { 0x94400000u, 0x38000000u, MB( 8), OatKind::Mmio  }, /* PCMCIA socket 1 mid window */
    { 0x94C00000u, 0x2C000000u, MB(64), OatKind::Mmio  }, /* PCMCIA socket 0 common-memory window */
    { 0x98C00000u, 0x3C000000u, MB(64), OatKind::Mmio  }, /* PCMCIA socket 1 common-memory window */
};

/* DRAM banks span PA 0xC0000000..0xC4000000 (64 MB) contiguously across the
   three Dram entries above. */
constexpr uint32_t kDramPaBase     = 0xC0000000u;
constexpr uint32_t kDramSize       = MB(64);
constexpr uint32_t kInitStackTopPa = kDramPaBase + kDramSize;

class SiemensSimpadPageTableBuilder : public PageTableBuilder {
public:
    using PageTableBuilder::PageTableBuilder;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimpad;
    }

    uint32_t InitStackTopPa() const override { return kInitStackTopPa; }
    uint32_t VaToPa(uint32_t va) const override;
    std::vector<DramRegion>   CachedDramRegions()   const override;
    std::vector<BackedRegion> BackedMemoryRegions() const override;
    std::vector<DramRegion>   MappedVaSpans()       const override;
};

uint32_t SiemensSimpadPageTableBuilder::VaToPa(uint32_t va) const {
    for (const auto& e : kOat) {
        if (va >= e.va_base && va < e.va_base + e.size) {
            return e.pa_base + (va - e.va_base);
        }
    }
    LOG(Caution, "SiemensSimpadPageTableBuilder::VaToPa: VA 0x%08X outside every "
            "OAT band (nk.exe OEMAddressTable @ 0x80081400)\n", va);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

std::vector<DramRegion> SiemensSimpadPageTableBuilder::CachedDramRegions() const {
    std::vector<DramRegion> regions;
    for (const auto& e : kOat) {
        if (e.kind == OatKind::Dram) {
            regions.push_back({ e.va_base, e.pa_base, e.size });
        }
    }
    return regions;
}

std::vector<BackedRegion>
SiemensSimpadPageTableBuilder::BackedMemoryRegions() const {
    std::vector<BackedRegion> regions;
    for (const auto& e : kOat) {
        if (e.kind == OatKind::Mmio) continue;
        const MemoryKind kind =
            (e.kind == OatKind::Flash) ? MemoryKind::kFlash : MemoryKind::kRam;
        regions.push_back({ e.va_base, e.pa_base, e.size, kind });
    }
    /* Back the boot stub's head copy-source flash window - no OAT entry maps
       a VA to it, so it is not covered by the loop above. */
    regions.push_back({ 0u, siemens_simpad::kHeadCopySrcPa,
                        siemens_simpad::kHeadLen, MemoryKind::kFlash });
    std::sort(regions.begin(), regions.end(),
              [](const BackedRegion& a, const BackedRegion& b) {
                  return a.pa_base < b.pa_base;
              });
    std::vector<BackedRegion> merged;
    for (const auto& r : regions) {
        if (!merged.empty()) {
            BackedRegion& m = merged.back();
            const uint32_t m_end = m.pa_base + m.size;
            if (r.kind == m.kind && r.pa_base <= m_end) {
                const uint32_t r_end = r.pa_base + r.size;
                if (r_end > m_end) m.size = r_end - m.pa_base;
                continue;
            }
        }
        merged.push_back(r);
    }
    return merged;
}

std::vector<DramRegion> SiemensSimpadPageTableBuilder::MappedVaSpans() const {
    std::vector<DramRegion> regions;
    for (const auto& e : kOat) {
        regions.push_back({ e.va_base, e.pa_base, e.size });
    }
    return regions;
}

}  /* namespace */

REGISTER_SERVICE_AS(SiemensSimpadPageTableBuilder, PageTableBuilder);
