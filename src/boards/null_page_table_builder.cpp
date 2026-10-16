#include "page_table_builder.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>

namespace {

class NullPageTableBuilder : public PageTableBuilder {
public:
    using PageTableBuilder::PageTableBuilder;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardId().empty();
    }

    uint32_t InitStackTopPa() const override { return 0; }
    uint32_t VaToPa(uint32_t va) const override { return va; }
    std::vector<DramRegion>   CachedDramRegions()   const override { return {}; }
    std::vector<BackedRegion> BackedMemoryRegions() const override { return {}; }
    std::vector<DramRegion>   MappedVaSpans()       const override { return {}; }
};

}  /* namespace */

REGISTER_SERVICE_AS(NullPageTableBuilder, PageTableBuilder);
