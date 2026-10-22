#include "../page_table_builder.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "sharp_brain_pw_sh4_family_id.h"
#include <cerfcore/cpu/emulated_memory.h>

#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint32_t MB(uint32_t mb) { return mb * 0x100000u; }

enum class OatKind { Dram, Ocram, Mmio };

struct OatEntry {
    uint32_t va_base;
    uint32_t pa_base;
    uint32_t size;
    OatKind  kind;
};

/* OEMAddressTable verbatim from sharp_brain_pw_sh4 nk.exe at VA 0x802010D8
   (triplets of UINT32 VA, PA, count_in_MB, terminated by a zero count). */
constexpr OatEntry kOat[] = {
    /* 1 */ { 0x90000000u, 0x80000000u, MB(  1), OatKind::Mmio  },
    /* 2 */ { 0x90100000u, 0x60000000u, MB(  1), OatKind::Mmio  },
    /* 3 */ { 0x80000000u, 0x40000000u, MB(128), OatKind::Dram  },
    /* 4 */ { 0x88000000u, 0x00000000u, MB(  1), OatKind::Ocram },
};

/* i.MX28 RM Rev. 2 Table 4-1: OCRAM 0x00000000-0x0001FFFF, 128 KB. */
constexpr uint32_t kOcramSize = 0x20000u;

constexpr uint32_t kDramPaBase     = 0x40000000u;
constexpr uint32_t kDramSize       = MB(128);
constexpr uint32_t kInitStackTopPa = kDramPaBase + kDramSize;

class SharpBrainPwSh4PageTableBuilder : public PageTableBuilder {
public:
    using PageTableBuilder::PageTableBuilder;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpBrainPwSh4;
    }

    uint32_t InitStackTopPa() const override { return kInitStackTopPa; }
    uint32_t VaToPa(uint32_t va) const override;
    std::vector<DramRegion>   CachedDramRegions()   const override;
    std::vector<BackedRegion> BackedMemoryRegions() const override;
    std::vector<DramRegion>   MappedVaSpans()       const override;
};

uint32_t SharpBrainPwSh4PageTableBuilder::VaToPa(uint32_t va) const {
    for (const auto& e : kOat) {
        if (va >= e.va_base && va < e.va_base + e.size) {
            return e.pa_base + (va - e.va_base);
        }
    }
    LOG(Caution, "SharpBrainPwSh4PageTableBuilder::VaToPa: VA 0x%08X outside "
            "every OAT band (nk.exe 0x802010D8)\n", va);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

std::vector<DramRegion> SharpBrainPwSh4PageTableBuilder::CachedDramRegions() const {
    std::vector<DramRegion> regions;
    for (const auto& e : kOat) {
        if (e.kind == OatKind::Dram) {
            regions.push_back({ e.va_base, e.pa_base, e.size });
        } else if (e.kind == OatKind::Ocram) {
            regions.push_back({ e.va_base, e.pa_base, kOcramSize });
        }
    }
    return regions;
}

std::vector<BackedRegion>
SharpBrainPwSh4PageTableBuilder::BackedMemoryRegions() const {
    std::vector<BackedRegion> regions;
    for (const auto& e : kOat) {
        if (e.kind == OatKind::Dram) {
            regions.push_back({ e.va_base, e.pa_base, e.size, MemoryKind::kRam });
        } else if (e.kind == OatKind::Ocram) {
            regions.push_back({ e.va_base, e.pa_base, kOcramSize, MemoryKind::kRam, e.size });
        }
    }
    return regions;
}

std::vector<DramRegion> SharpBrainPwSh4PageTableBuilder::MappedVaSpans() const {
    std::vector<DramRegion> regions;
    for (const auto& e : kOat) {
        regions.push_back({ e.va_base, e.pa_base, e.size });
    }
    return regions;
}

}  /* namespace */

REGISTER_SERVICE_AS(SharpBrainPwSh4PageTableBuilder, PageTableBuilder);

}
