#include "../page_table_builder.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "pangolin_family_id.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint32_t MB(uint32_t mb) { return mb * 0x100000u; }

enum class OatKind { Dram, Flash, Mmio };

struct OatEntry {
    uint32_t va_base;
    uint32_t pa_base;
    uint32_t size;
    OatKind  kind;
};

/* SmartBook G138 OEMAddressTable, decoded from nk.exe at IDA VA 0x8C1438DC
   (kernel links 0x8C140000 = carved OS XIP physfirst); startup walks it as
   (VA,PA,sizeMB) triplets to a {0,0,0} terminator (sub_8C144BA8). Per-entry PA
   kind per Intel SA-1110 Dev Man §2.4; kernel runs cached from DRAM @ 0xC0140000. */
constexpr OatEntry kOat[] = {
    { 0x9E000000u, 0x3C000000u, MB(32), OatKind::Mmio  }, /* PCMCIA socket 1 (PA 0x30-0x40M region) */
    { 0x9C000000u, 0x38000000u, MB(32), OatKind::Mmio  }, /* PCMCIA socket 1 */
    { 0x9A000000u, 0x30000000u, MB(32), OatKind::Mmio  }, /* PCMCIA socket 1 base */
    { 0x98000000u, 0x2C000000u, MB(32), OatKind::Mmio  }, /* PCMCIA socket 0 (PA 0x20-0x30M region) */
    { 0x96000000u, 0x28000000u, MB(32), OatKind::Mmio  }, /* PCMCIA socket 0 */
    { 0x94000000u, 0x20000000u, MB(32), OatKind::Mmio  }, /* PCMCIA socket 0 base */
    { 0x8E000000u, 0xC2000000u, MB(96), OatKind::Dram  }, /* DRAM bank 0, +32MB..128MB (cached) */
    { 0x8C000000u, 0xC0000000u, MB(32), OatKind::Dram  }, /* DRAM bank 0 base (cached) - NK image @ 0xC0140000 */
    { 0x8BD00000u, 0x08000000u, MB( 1), OatKind::Flash }, /* static bank CS1 (NOR) 1MB window */
    { 0x8BA00000u, 0x42000000u, MB( 1), OatKind::Mmio  }, /* variable-latency I/O (PA 0x40-0x48M region) */
    { 0x8B600000u, 0x4B800000u, MB( 2), OatKind::Mmio  }, /* variable-latency I/O */
    { 0x8B400000u, 0x4BE00000u, MB( 2), OatKind::Mmio  }, /* variable-latency I/O */
    { 0x8B000000u, 0xB0000000u, MB( 4), OatKind::Mmio  }, /* SA-1110 LCD / DMA controller */
    { 0x8A000000u, 0xA0000000u, MB( 4), OatKind::Mmio  }, /* SA-1110 memory & expansion controller */
    { 0x89000000u, 0x90000000u, MB( 4), OatKind::Mmio  }, /* SA-1110 system control (OST/RTC/GPIO/INTC/power) */
    { 0x88C00000u, 0xE0000000u, MB( 4), OatKind::Mmio  }, /* SA-1110 zeros bank / cache-flush stripe */
    { 0x88000000u, 0x80000000u, MB( 4), OatKind::Mmio  }, /* SA-1110 peripheral control (PPC/UART/MCP/SSP/UDC) */
    { 0x86000000u, 0x08000000u, MB(32), OatKind::Flash }, /* static bank CS1 (NOR) full, uncached view */
    { 0x82000000u, 0xD0000000u, MB(32), OatKind::Dram  }, /* DRAM bank 2 (cached) */
    { 0x84000000u, 0x00000000u, MB(32), OatKind::Flash }, /* static bank CS0 (NOR boot ROM/flash) */
};

/* DRAM bank 0 spans PA 0xC0000000..0xC8000000 (128 MB) contiguously across the
   two bank-0 entries above; the pre-MMU bootloader-handoff stack tops it. */
constexpr uint32_t kBank0PaBase    = 0xC0000000u;
constexpr uint32_t kBank0Size      = MB(128);
constexpr uint32_t kInitStackTopPa = kBank0PaBase + kBank0Size;

class PangolinPageTableBuilder : public PageTableBuilder {
public:
    using PageTableBuilder::PageTableBuilder;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::Pangolin;
    }

    uint32_t InitStackTopPa() const override { return kInitStackTopPa; }
    uint32_t VaToPa(uint32_t va) const override;
    std::vector<DramRegion>   CachedDramRegions()   const override;
    std::vector<BackedRegion> BackedMemoryRegions() const override;
    std::vector<DramRegion>   MappedVaSpans()       const override;
};

uint32_t PangolinPageTableBuilder::VaToPa(uint32_t va) const {
    for (const auto& e : kOat) {
        if (va >= e.va_base && va < e.va_base + e.size) {
            return e.pa_base + (va - e.va_base);
        }
    }
    LOG(Caution, "PangolinPageTableBuilder::VaToPa: VA 0x%08X outside every "
            "OAT band (nk.exe OEMAddressTable @ 0x8C1438DC)\n", va);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

std::vector<DramRegion> PangolinPageTableBuilder::CachedDramRegions() const {
    std::vector<DramRegion> regions;
    for (const auto& e : kOat) {
        if (e.kind == OatKind::Dram) {
            regions.push_back({ e.va_base, e.pa_base, e.size });
        }
    }
    return regions;
}

std::vector<BackedRegion>
PangolinPageTableBuilder::BackedMemoryRegions() const {
    std::vector<BackedRegion> regions;
    for (const auto& e : kOat) {
        if (e.kind == OatKind::Mmio) continue;
        const MemoryKind kind =
            (e.kind == OatKind::Flash) ? MemoryKind::kFlash : MemoryKind::kRam;
        regions.push_back({ e.va_base, e.pa_base, e.size, kind });
    }
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

std::vector<DramRegion> PangolinPageTableBuilder::MappedVaSpans() const {
    std::vector<DramRegion> regions;
    for (const auto& e : kOat) {
        regions.push_back({ e.va_base, e.pa_base, e.size });
    }
    return regions;
}

}  /* namespace */

REGISTER_SERVICE_AS(PangolinPageTableBuilder, PageTableBuilder);

}
