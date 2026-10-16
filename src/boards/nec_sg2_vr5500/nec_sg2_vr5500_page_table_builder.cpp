#include "../mips_kseg_page_table_builder.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfengine/mips/mips_mmu.h>
#include <cerfcore/boards/board_context.h>
#include "nec_sg2_vr5500_family_id.h"

#include <cstdint>
#include <vector>

namespace {

constexpr uint32_t kDramPaBase = 0x00000000u;
constexpr uint32_t kDramSize   = 0x08000000u;
constexpr uint32_t kDramVaBase = cerfengine::MipsSeg::kKusegEnd | kDramPaBase;

class NecSg2Vr5500PageTableBuilder : public MipsKsegPageTableBuilder {
public:
    using MipsKsegPageTableBuilder::MipsKsegPageTableBuilder;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecSg2Vr5500;
    }

    std::vector<DramRegion> CachedDramRegions() const override {
        return { { kDramVaBase, kDramPaBase, kDramSize } };
    }

    std::vector<BackedRegion> BackedMemoryRegions() const override {
        return { { kDramVaBase, kDramPaBase, kDramSize, MemoryKind::kRam } };
    }

    std::vector<DramRegion> MappedVaSpans() const override {
        return { { kDramVaBase, kDramPaBase, kDramSize } };
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(NecSg2Vr5500PageTableBuilder, PageTableBuilder);
