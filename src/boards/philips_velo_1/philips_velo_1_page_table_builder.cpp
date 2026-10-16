#include "../mips_kseg_page_table_builder.h"

#include <cerfcore/boot/rom_parser_service.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "philips_velo_1_dram_card_profile.h"
#include "philips_velo_1_family_id.h"

#include <cstdint>
#include <vector>

namespace {

constexpr uint32_t kDramVaBase = 0x80000000u;

/* nk.exe sub_9F40EA9C leaves ENCS1DRAM clear (MEMCONFIG0 &= 0x010FFFF1), so PA 0
   decodes DRAM BANK 0 (TMPR3911 Table 4.2.1). */
constexpr uint32_t kDramPaBase = 0x00000000u;

/* The Philips Velo 1 ships 4 MB of EDO DRAM (OEM specification). nk.exe
   sub_9F40EAE0 measures DRAM by aliasing, so the guest adopts whatever size is
   populated here. */
constexpr uint32_t kDramSize = 0x00400000u;

/* DRAM BANK 0 decodes 32 MB at PA 0 (TMPR3911 Table 4.2.1); the 4 MB population
   aliases through the window, which is how sub_9F40EAE0 sizes it. */
constexpr uint32_t kDramDecodeSpan = 0x02000000u;

/* philips_velo_1_ce1 nk.exe sub_9F42453C @0x9F42453C reports the slot-1 card base as
   0x82000000; PA 0x02000000 decodes 32 MB as DRAM BANK 1 while CS1 is not enabled
   (TMPR3911 Table 4.2.1). */
constexpr uint32_t kBank1VaBase     = 0x82000000u;
constexpr uint32_t kBank1PaBase     = 0x02000000u;
constexpr uint32_t kBank1DecodeSpan = 0x02000000u;

/* Internal Function Registers, 2 MB at PA 0x10C00000 (TMPR3911 Table 4.2.1). */
constexpr uint32_t kRegsPaBase = 0x10C00000u;
constexpr uint32_t kRegsVaBase = 0xB0C00000u;
constexpr uint32_t kRegsSize   = 0x00200000u;

class PhilipsVelo1PageTableBuilder : public MipsKsegPageTableBuilder {
public:
    using MipsKsegPageTableBuilder::MipsKsegPageTableBuilder;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::PhilipsVelo1;
    }

    void OnReady() override {
        const ParsedRom& prim = PrimaryRom();

        /* Both XIPs are resident in the same CS0 flash, so the backed region spans
           the first XIP's physfirst to the last XIP's physlast. */
        uint32_t rom_va_base = 0xFFFFFFFFu;
        uint32_t rom_va_end  = 0u;
        for (const auto& xip : prim.xips) {
            const ParsedROMHDR& h = xip.toc.romhdr;
            if (h.physfirst < rom_va_base) rom_va_base = h.physfirst;
            if (h.physlast  > rom_va_end)  rom_va_end  = h.physlast;
        }

        rom_ = Kseg0RomSpan(rom_va_base, rom_va_end);

        const ParsedROMHDR* hdr = prim.XipHeaderContaining(prim.entry_va);
        if (!hdr) {
            emu_.Get<Fatal>().Die("PhilipsVelo1PageTableBuilder: entry VA 0x%08X lies "
                                  "in no XIP", prim.entry_va);
        }

        RequireRamEndWithin(*hdr, kDramVaBase, kDramSize);

        card_ = FindPhilipsVelo1DramCard(emu_.Get<BoardContext>().GetBoardId());

        LOG(Boot, "PhilipsVelo1PageTableBuilder: ROM kva=0x%08X pa=0x%08X size=0x%X, "
                  "DRAM pa=0x%08X size=0x%X span=0x%X (ulRAMEnd=0x%08X), "
                  "DRAM card size=0x%X\n",
            rom_.va_base, rom_.pa_base, rom_.size, kDramPaBase, kDramSize,
            kDramDecodeSpan, hdr->ulRAMEnd, card_ ? card_->Bytes() : 0u);
    }

    std::vector<DramRegion> CachedDramRegions() const override {
        std::vector<DramRegion> r = { { kDramVaBase, kDramPaBase, kDramSize } };
        if (card_) r.push_back({ kBank1VaBase, kBank1PaBase, card_->Bytes() });
        return r;
    }

    std::vector<BackedRegion> BackedMemoryRegions() const override {
        std::vector<BackedRegion> r = {
            { kDramVaBase, kDramPaBase, kDramSize, MemoryKind::kRam, kDramDecodeSpan },
        };
        if (card_) {
            r.push_back({ kBank1VaBase, kBank1PaBase, card_->Bytes(), MemoryKind::kRam,
                          kBank1DecodeSpan });
        }
        r.push_back({ rom_.va_base, rom_.pa_base, rom_.size, MemoryKind::kRom });
        return r;
    }

    std::vector<DramRegion> MappedVaSpans() const override {
        std::vector<DramRegion> r = { { kDramVaBase, kDramPaBase, kDramDecodeSpan } };
        if (card_) r.push_back({ kBank1VaBase, kBank1PaBase, kBank1DecodeSpan });
        r.push_back(rom_);
        r.push_back({ kRegsVaBase, kRegsPaBase, kRegsSize });
        return r;
    }

private:
    DramRegion rom_{};
    const PhilipsVelo1DramCardProfile* card_ = nullptr;
};

}  /* namespace */

REGISTER_SERVICE_AS(PhilipsVelo1PageTableBuilder, PageTableBuilder);
