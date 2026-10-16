#include "../mips/mips_tlb_model.h"

#include <cstdint>

#include <cerfcore/boards/board_context.h>
#include <cerfengine/mips/mips_cpu_state.h>
#include "../../socs/vr4102/vr4102_id.h"
#include "../../socs/vr4111/vr4111_id.h"
#include "../../socs/vr4121/vr4121_id.h"
#include "../../socs/vr4122/vr4122_id.h"
#include "../vr5500/vr5500_id.h"
#include <cerfcore/core/cerf_emulator.h>

/* QEMU MIPS r4k_* (tlb_helper.c). */

namespace {

/* 8-bit EntryHi ASID: VR5500, VR4102, VR4121 (VR4121 UM Fig 6-17), VR4122
   (VR4100 Series UM U15509EJ2V0UM 5.4), VR4111 (VR4111 UM Fig 6-17 p.179). */
constexpr uint32_t kAsidMask = 0x000000FF;

/* QEMU get_tlb_pfn_from_entrylo (tlb_helper.c). */
inline uint32_t TlbPfnFromEntryLo(uint32_t entrylo) {
    return (entrylo >> 6) & 0x00FFFFFFu;
}

/* QEMU get_entrylo_pfn_from_tlb (tlb_helper.c). */
inline uint32_t EntryLoPfnFromTlb(uint64_t pfn, uint32_t s) {
    const uint64_t tlb_pfn = pfn >> s;
    return static_cast<uint32_t>((tlb_pfn & 0x00FFFFFFu) << 6);
}

class MipsMmuR4000 final : public MipsTlbModel {
public:
    using MipsTlbModel::MipsTlbModel;

    bool ShouldRegister() override {
        const std::string_view soc = emu_.Get<BoardContext>().GetSocId();
        return soc == SocId::Vr4102 || soc == SocId::Vr4111 || soc == SocId::Vr4121 ||
               soc == SocId::Vr4122 || soc == SocId::Vr5500;
    }

    cerfengine::MipsTlbHooks Hooks() override {
        return cerfengine::MipsTlbHooks{this,          &MapAddress,   &MappedPageGlobal,
                                        &WriteIndexed, &WriteRandom, &Probe, &Read};
    }

    uint32_t RandomIndex(cerfengine::MipsCpuState* st) override {
        return NextRandom(st->cp0_wired, st->nb_tlb - st->cp0_wired);
    }

private:
    static void CERFENGINE_FASTCALL WriteIndexed(void*, cerfengine::MipsTlbContext& ctx) {
        cerfengine::MipsCpuState* st = ctx.State();
        /* QEMU r4k_helper_tlbwi (tlb_helper.c). */
        const uint16_t asid = st->cp0_entryhi & kAsidMask;
        const uint32_t idx  = (st->cp0_index & ~0x80000000u) % st->nb_tlb;
        const cerfengine::MipsTlbEntry& e = st->tlb[idx];
        const uint32_t vpn = st->cp0_entryhi & cerfengine::MipsVpn2Mask(st->min_page_shift);
        const uint8_t  g   = static_cast<uint8_t>(st->cp0_entrylo0 & st->cp0_entrylo1 & 1u);
        const bool v0 = (st->cp0_entrylo0 & 2u) != 0;
        const bool d0 = (st->cp0_entrylo0 & 4u) != 0;
        const bool v1 = (st->cp0_entrylo1 & 2u) != 0;
        const bool d1 = (st->cp0_entrylo1 & 4u) != 0;
        if (e.vpn != vpn || e.asid != asid || e.g != g ||
            (e.v0 && !v0) || (e.d0 && !d0) ||
            (e.v1 && !v1) || (e.d1 && !d1)) {
            FlushExtra(ctx, st->nb_tlb);
        }
        InvalidateTlb(ctx, idx, false);
        FillTlb(st, idx);
    }

    static void CERFENGINE_FASTCALL WriteRandom(void* self, cerfengine::MipsTlbContext& ctx) {
        cerfengine::MipsCpuState* st = ctx.State();
        /* QEMU r4k_helper_tlbwr (tlb_helper.c). */
        const uint32_t r = static_cast<MipsMmuR4000*>(self)->RandomIndex(st);
        InvalidateTlb(ctx, r, true);
        FillTlb(st, r);
    }

    static void CERFENGINE_FASTCALL Probe(void*, cerfengine::MipsTlbContext& ctx) {
        cerfengine::MipsCpuState* st = ctx.State();
        /* QEMU r4k_helper_tlbp (tlb_helper.c). */
        const uint16_t asid = st->cp0_entryhi & kAsidMask;
        for (uint32_t i = 0; i < st->nb_tlb; i++) {
            const cerfengine::MipsTlbEntry& e = st->tlb[i];
            const uint32_t mask = e.page_mask | cerfengine::MipsPairMinMask(st->min_page_shift);
            const uint32_t tag  = st->cp0_entryhi & ~mask;
            const uint32_t vpn  = e.vpn & ~mask;
            if ((e.g || e.asid == asid) && vpn == tag) {
                st->cp0_index = i;
                return;
            }
        }
        for (uint32_t i = st->nb_tlb; i < st->tlb_in_use; i++) {
            const cerfengine::MipsTlbEntry& e = st->tlb[i];
            const uint32_t mask = e.page_mask | cerfengine::MipsPairMinMask(st->min_page_shift);
            const uint32_t tag  = st->cp0_entryhi & ~mask;
            const uint32_t vpn  = e.vpn & ~mask;
            if ((e.g || e.asid == asid) && vpn == tag) {
                FlushExtra(ctx, i);
                break;
            }
        }
        st->cp0_index |= 0x80000000u;  /* probe failed: P bit */
    }

    static void CERFENGINE_FASTCALL Read(void*, cerfengine::MipsTlbContext& ctx) {
        cerfengine::MipsCpuState* st = ctx.State();
        /* QEMU r4k_helper_tlbr (tlb_helper.c). */
        const uint16_t asid = st->cp0_entryhi & kAsidMask;
        const uint32_t idx  = (st->cp0_index & ~0x80000000u) % st->nb_tlb;
        const cerfengine::MipsTlbEntry& e = st->tlb[idx];
        if (asid != e.asid) {
            FlushAll(ctx);
        }
        FlushExtra(ctx, st->nb_tlb);
        st->cp0_entryhi  = e.vpn | e.asid;
        st->cp0_pagemask = e.page_mask;
        st->cp0_entrylo0 = e.g | (static_cast<uint32_t>(e.v0) << 1) |
                           (static_cast<uint32_t>(e.d0) << 2) |
                           (static_cast<uint32_t>(e.c0) << 3) | EntryLoPfnFromTlb(e.pfn[0], st->min_page_shift);
        st->cp0_entrylo1 = e.g | (static_cast<uint32_t>(e.v1) << 1) |
                           (static_cast<uint32_t>(e.d1) << 2) |
                           (static_cast<uint32_t>(e.c1) << 3) | EntryLoPfnFromTlb(e.pfn[1], st->min_page_shift);
    }

    static cerfengine::MipsTlbResult CERFENGINE_FASTCALL MapAddress(
        void*, cerfengine::MipsCpuState* st, uint32_t va, cerfengine::MipsAccess acc,
        uint32_t* pa) {
        /* QEMU r4k_map_address (tlb_helper.c). */
        const uint16_t asid = st->cp0_entryhi & kAsidMask;
        for (uint32_t i = 0; i < st->tlb_in_use; i++) {
            const cerfengine::MipsTlbEntry& e = st->tlb[i];
            const uint32_t mask = e.page_mask | cerfengine::MipsPairMinMask(st->min_page_shift);
            const uint32_t tag  = va & ~mask;
            const uint32_t vpn  = e.vpn & ~mask;
            if (!((e.g || e.asid == asid) && vpn == tag)) {
                continue;
            }
            const uint32_t n = (va & mask & ~(mask >> 1)) ? 1u : 0u;
            if (!(n ? e.v1 : e.v0)) {
                return cerfengine::MipsTlbResult::kInvalid;
            }
            if (acc != cerfengine::MipsAccess::kWrite || (n ? e.d1 : e.d0)) {
                /* SoC physical-space mirror (VR4102 UM Table 5-6): a PFN above
                   0x20000000 aliases into 0x0-0x1FFFFFFF. */
                *pa = static_cast<uint32_t>(e.pfn[n] | (va & (mask >> 1))) & st->phys_addr_mask;
                return cerfengine::MipsTlbResult::kMatch;
            }
            return cerfengine::MipsTlbResult::kModified;
        }
        return cerfengine::MipsTlbResult::kNoMatch;
    }

    static bool CERFENGINE_FASTCALL MappedPageGlobal(void*, cerfengine::MipsCpuState* st,
                                                     uint32_t va) {
        /* QEMU r4k_map_address (tlb_helper.c). */
        const uint16_t asid = st->cp0_entryhi & kAsidMask;
        for (uint32_t i = 0; i < st->tlb_in_use; i++) {
            const cerfengine::MipsTlbEntry& e = st->tlb[i];
            const uint32_t mask = e.page_mask | cerfengine::MipsPairMinMask(st->min_page_shift);
            if ((e.g || e.asid == asid) && (e.vpn & ~mask) == (va & ~mask)) {
                return e.g != 0;
            }
        }
        return false;
    }

    static void FillTlb(cerfengine::MipsCpuState* st, uint32_t idx) {
        /* QEMU r4k_fill_tlb (tlb_helper.c). */
        const uint32_t mask = st->cp0_pagemask >> (st->min_page_shift + 1u);
        cerfengine::MipsTlbEntry& e = st->tlb[idx];
        e.vpn       = st->cp0_entryhi & cerfengine::MipsVpn2Mask(st->min_page_shift);
        e.asid      = st->cp0_entryhi & kAsidMask;
        e.page_mask = st->cp0_pagemask;
        e.g  = static_cast<uint8_t>(st->cp0_entrylo0 & st->cp0_entrylo1 & 1u);
        e.v0 = static_cast<uint8_t>((st->cp0_entrylo0 & 2u) != 0);
        e.d0 = static_cast<uint8_t>((st->cp0_entrylo0 & 4u) != 0);
        e.c0 = static_cast<uint8_t>((st->cp0_entrylo0 >> 3) & 7u);
        e.pfn[0] = (static_cast<uint64_t>(TlbPfnFromEntryLo(st->cp0_entrylo0) & ~mask)) << st->min_page_shift;
        e.v1 = static_cast<uint8_t>((st->cp0_entrylo1 & 2u) != 0);
        e.d1 = static_cast<uint8_t>((st->cp0_entrylo1 & 4u) != 0);
        e.c1 = static_cast<uint8_t>((st->cp0_entrylo1 >> 3) & 7u);
        e.pfn[1] = (static_cast<uint64_t>(TlbPfnFromEntryLo(st->cp0_entrylo1) & ~mask)) << st->min_page_shift;
    }

    static void InvalidateTlb(cerfengine::MipsTlbContext& ctx, uint32_t idx, bool use_extra) {
        cerfengine::MipsCpuState* st = ctx.State();
        /* QEMU r4k_invalidate_tlb (tlb_helper.c). */
        cerfengine::MipsTlbEntry& e = st->tlb[idx];
        const uint16_t asid = st->cp0_entryhi & kAsidMask;
        if (e.g == 0 && e.asid != asid) {
            return;
        }
        if (use_extra && st->tlb_in_use < cerfengine::kMipsTlbMax) {
            st->tlb[st->tlb_in_use] = e;
            st->tlb_in_use++;
            return;
        }
        const uint32_t mask = e.page_mask | cerfengine::MipsPairMinMask(st->min_page_shift);
        if (e.v0) {
            uint32_t addr = e.vpn & ~mask;
            const uint32_t end = addr | (mask >> 1);
            while (addr < end) {
                ctx.InvalidatePage(addr);
                addr += 0x1000u;
            }
        }
        if (e.v1) {
            uint32_t addr = (e.vpn & ~mask) | ((mask >> 1) + 1u);
            const uint32_t end = addr | mask;
            while (addr - 1u < end) {
                ctx.InvalidatePage(addr);
                addr += 0x1000u;
            }
        }
    }

    static void FlushExtra(cerfengine::MipsTlbContext& ctx, uint32_t first) {
        cerfengine::MipsCpuState* st = ctx.State();
        /* QEMU r4k_mips_tlb_flush_extra (tlb_helper.c). */
        while (st->tlb_in_use > first) {
            InvalidateTlb(ctx, --st->tlb_in_use, false);
        }
    }
};

}  // namespace

REGISTER_SERVICE_AS(MipsMmuR4000, MipsTlbModel);
