#include "../page_table_builder.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_900_series_family_id.h"

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

/* NEC MobilePro 900 (P530) OEMAddressTable, decoded from nk.exe at IDA
   0x90201E38 ((VA,PA,sizeMB) triples to a {0,0,0} terminator). PXA255 PA map
   per Developer's Manual 278693. */
constexpr OatEntry kOat[] = {
    { 0x80000000u, 0x00000000u, MB(64), OatKind::Flash }, /* static CS0 (boot NOR) */
    /* CS1 is Flash not Mmio: the OS-chain XIP region (physfirst 0x84080000 ->
       PA 0x04080000) lives here - read-only ROM TOC + module content that must
       be backed, else module loads fault. */
    { 0x84000000u, 0x04000000u, MB(64), OatKind::Flash }, /* static CS1 - OS-chain XIP image */
    { 0x88000000u, 0x08000000u, MB( 1), OatKind::Mmio  }, /* static CS2 board device */
    { 0x88100000u, 0x09000000u, MB( 1), OatKind::Mmio  },
    { 0x88200000u, 0x0A000000u, MB( 1), OatKind::Mmio  },
    { 0x88300000u, 0x0B000000u, MB( 1), OatKind::Mmio  },
    { 0x88400000u, 0x0C000000u, MB( 1), OatKind::Mmio  }, /* static CS3 board device */
    { 0x88500000u, 0x0C200000u, MB( 2), OatKind::Mmio  },
    { 0x88700000u, 0x0D000000u, MB( 1), OatKind::Mmio  },
    { 0x88800000u, 0x0E000000u, MB( 1), OatKind::Mmio  },
    { 0x88D00000u, 0x13000000u, MB( 1), OatKind::Mmio  },
    { 0x88E00000u, 0x13300000u, MB( 1), OatKind::Mmio  },
    { 0x8A000000u, 0x20000000u, MB( 1), OatKind::Mmio  }, /* PCMCIA socket 0 attr/IO */
    { 0x8A300000u, 0x23000000u, MB( 1), OatKind::Mmio  },
    { 0x8A400000u, 0x28000000u, MB( 1), OatKind::Mmio  },
    { 0x8A700000u, 0x2B000000u, MB( 1), OatKind::Mmio  },
    { 0x8A800000u, 0x30000000u, MB( 1), OatKind::Mmio  }, /* PCMCIA socket 1 attr/IO */
    { 0x8A900000u, 0x31000000u, MB( 1), OatKind::Mmio  },
    { 0x8AA00000u, 0x32000000u, MB( 1), OatKind::Mmio  },
    { 0x8AB00000u, 0x33000000u, MB( 1), OatKind::Mmio  },
    { 0x8AC00000u, 0x38000000u, MB( 1), OatKind::Mmio  },
    { 0x8AD00000u, 0x39000000u, MB( 1), OatKind::Mmio  },
    { 0x8AE00000u, 0x3A000000u, MB( 1), OatKind::Mmio  },
    { 0x8AF00000u, 0x3B000000u, MB( 1), OatKind::Mmio  },
    { 0x8B000000u, 0x2C000000u, MB(16), OatKind::Mmio  }, /* PCMCIA socket 0 common */
    { 0x8E000000u, 0x2F000000u, MB(16), OatKind::Mmio  },
    { 0x90000000u, 0xA0000000u, MB(64), OatKind::Dram  }, /* SDRAM - kernel (VA 0x90200000) + RAM */
    { 0x94000000u, 0x40000000u, MB(32), OatKind::Mmio  }, /* PXA255 peripherals (DMA/UART/INTC/GPIO/OST/clk) */
    { 0x96000000u, 0x44000000u, MB( 1), OatKind::Mmio  }, /* PXA255 LCD controller */
    { 0x96100000u, 0x48000000u, MB( 1), OatKind::Mmio  }, /* PXA255 memory controller regs */
    { 0x96200000u, 0xB0000000u, MB( 1), OatKind::Mmio  }, /* board/cache window */
    { 0x98000000u, 0x3C000000u, MB(16), OatKind::Mmio  }, /* PCMCIA socket 1 common */
    { 0x99000000u, 0x3D000000u, MB(16), OatKind::Mmio  },
    { 0x9A000000u, 0x3E000000u, MB(16), OatKind::Mmio  },
    { 0x9B000000u, 0x3F000000u, MB(16), OatKind::Mmio  },
};

/* SDRAM is 64 MB at PA 0xA0000000 (OAT band VA 0x90000000). Bootloader-handoff
   SP = top of SDRAM. */
constexpr uint32_t kDramPaBase     = 0xA0000000u;
constexpr uint32_t kDramSize       = MB(64);
constexpr uint32_t kInitStackTopPa = kDramPaBase + kDramSize;

class NecMobilepro900SeriesPageTableBuilder : public PageTableBuilder {
public:
    using PageTableBuilder::PageTableBuilder;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecMobilepro900Series;
    }

    uint32_t InitStackTopPa() const override { return kInitStackTopPa; }
    uint32_t VaToPa(uint32_t va) const override;
    std::vector<DramRegion>   CachedDramRegions()   const override;
    std::vector<BackedRegion> BackedMemoryRegions() const override;
    std::vector<DramRegion>   MappedVaSpans()       const override;
};

uint32_t NecMobilepro900SeriesPageTableBuilder::VaToPa(uint32_t va) const {
    for (const auto& e : kOat) {
        if (va >= e.va_base && va < e.va_base + e.size) {
            return e.pa_base + (va - e.va_base);
        }
    }
    LOG(Caution, "NecMobilepro900SeriesPageTableBuilder::VaToPa: VA 0x%08X outside "
            "every OAT band (nk.exe OEMAddressTable @ 0x90201E38)\n", va);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

std::vector<DramRegion>
NecMobilepro900SeriesPageTableBuilder::CachedDramRegions() const {
    std::vector<DramRegion> regions;
    for (const auto& e : kOat) {
        if (e.kind == OatKind::Dram) {
            regions.push_back({ e.va_base, e.pa_base, e.size });
        }
    }
    return regions;
}

std::vector<BackedRegion>
NecMobilepro900SeriesPageTableBuilder::BackedMemoryRegions() const {
    std::vector<BackedRegion> regions;
    for (const auto& e : kOat) {
        if (e.kind == OatKind::Mmio) continue;
        const MemoryKind kind =
            (e.kind == OatKind::Flash) ? MemoryKind::kFlash : MemoryKind::kRam;
        regions.push_back({ e.va_base, e.pa_base, e.size, kind });
    }
    return regions;
}

std::vector<DramRegion> NecMobilepro900SeriesPageTableBuilder::MappedVaSpans() const {
    std::vector<DramRegion> regions;
    for (const auto& e : kOat) {
        regions.push_back({ e.va_base, e.pa_base, e.size });
    }
    return regions;
}

}  /* namespace */

REGISTER_SERVICE_AS(NecMobilepro900SeriesPageTableBuilder, PageTableBuilder);

}
