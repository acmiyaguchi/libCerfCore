#include "../mips/mips_tlb_model.h"

#include <cstdint>

#include <cerfcore/boards/board_context.h>
#include "../../socs/pr31x00/pr31500_id.h"
#include "../../socs/pr31x00/pr31700_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfengine/mips/mips_cpu_state.h>

/* TX39/H TLB: 32 entries, one 4-KB page each. TMPR3911.pdf Figure 3.3.6 (PDF
   p.96) and Figures 3.3.8 / 3.3.9 (PDF p.99). */
namespace {

/* EntryHi: VPN is "Bits 31..12 of virtual address"; PID is a 6-bit field. */
constexpr uint32_t kVpnMask  = 0xFFFFF000u;
constexpr uint32_t kPidShift = 6u;
constexpr uint32_t kPidMask  = 0x3Fu;

/* EntryLo: PFN[31:12] N[11] D[10] V[9] G[8], reserved[7:0]. */
constexpr uint32_t kPfnMask  = 0xFFFFF000u;
constexpr uint32_t kEntryLoN = 1u << 11;
constexpr uint32_t kEntryLoD = 1u << 10;
constexpr uint32_t kEntryLoV = 1u << 9;
constexpr uint32_t kEntryLoG = 1u << 8;

/* Index: P[31] "set to 1 when the last TLBProbe instruction was unsuccessful",
   Index[12:8]. Random holds its index in the same 12:8 field. */
constexpr uint32_t kIndexProbeFail = 1u << 31;
constexpr uint32_t kIndexShift     = 8u;
constexpr uint32_t kIndexMask      = 0x1Fu;

/* "The first eight entries (0 to 7) are the 'safe' entries because a tlbwr
   instruction can never replace the contents of these entries." */
constexpr uint32_t kRandomFirst = 8u;

constexpr uint32_t kNoEntry = 0xFFFFFFFFu;

class MipsMmuR3000 final : public MipsTlbModel {
public:
    using MipsTlbModel::MipsTlbModel;

    bool ShouldRegister() override {
        const std::string_view soc = emu_.Get<BoardContext>().GetSocId();
        return soc == SocId::Pr31500 || soc == SocId::Pr31700;
    }

    cerfengine::MipsTlbHooks Hooks() override {
        return cerfengine::MipsTlbHooks{this,          &MapAddress,   &MappedPageGlobal,
                                        &WriteIndexed, &WriteRandom, &Probe, &Read};
    }

    uint32_t RandomIndex(cerfengine::MipsCpuState* st) override {
        return NextRandom(kRandomFirst, st->nb_tlb - kRandomFirst);
    }

private:
    static void CERFENGINE_FASTCALL WriteIndexed(void*, cerfengine::MipsTlbContext& ctx) {
        cerfengine::MipsCpuState* st = ctx.State();
        Replace(ctx, (st->cp0_index >> kIndexShift) & kIndexMask);
    }

    static void CERFENGINE_FASTCALL WriteRandom(void* self, cerfengine::MipsTlbContext& ctx) {
        Replace(ctx, static_cast<MipsMmuR3000*>(self)->RandomIndex(ctx.State()));
    }

    static void CERFENGINE_FASTCALL Probe(void*, cerfengine::MipsTlbContext& ctx) {
        cerfengine::MipsCpuState* st = ctx.State();
        const uint32_t idx = Find(st, st->cp0_entryhi & kVpnMask, Pid(st));
        if (idx == kNoEntry) {
            st->cp0_index |= kIndexProbeFail;
            return;
        }
        st->cp0_index = idx << kIndexShift;
    }

    static void CERFENGINE_FASTCALL Read(void*, cerfengine::MipsTlbContext& ctx) {
        cerfengine::MipsCpuState* st = ctx.State();
        const cerfengine::MipsTlbEntry& e = st->tlb[(st->cp0_index >> kIndexShift) & kIndexMask];
        st->cp0_entryhi  = e.vpn | ((e.asid & kPidMask) << kPidShift);
        st->cp0_entrylo0 = static_cast<uint32_t>(e.pfn[0]) & kPfnMask;
        if (e.c0) st->cp0_entrylo0 |= kEntryLoN;
        if (e.d0) st->cp0_entrylo0 |= kEntryLoD;
        if (e.v0) st->cp0_entrylo0 |= kEntryLoV;
        if (e.g)  st->cp0_entrylo0 |= kEntryLoG;
    }

    static cerfengine::MipsTlbResult CERFENGINE_FASTCALL MapAddress(
        void*, cerfengine::MipsCpuState* st, uint32_t va, cerfengine::MipsAccess acc,
        uint32_t* pa) {
        const uint32_t idx = Find(st, va & kVpnMask, Pid(st));
        if (idx == kNoEntry) return cerfengine::MipsTlbResult::kNoMatch;

        const cerfengine::MipsTlbEntry& e = st->tlb[idx];
        if (!e.v0) return cerfengine::MipsTlbResult::kInvalid;
        /* "If an entry is accessed for a write operation when the D bit is
           cleared, the TX39/H causes a TLB Mod trap." (TMPR3911.pdf PDF p.96, §3.3.6.8). */
        if (acc == cerfengine::MipsAccess::kWrite && !e.d0) return cerfengine::MipsTlbResult::kModified;

        *pa = (static_cast<uint32_t>(e.pfn[0]) | (va & ~kVpnMask)) & st->phys_addr_mask;
        return cerfengine::MipsTlbResult::kMatch;
    }

    static bool CERFENGINE_FASTCALL MappedPageGlobal(void*, cerfengine::MipsCpuState* st,
                                                     uint32_t va) {
        const uint32_t idx = Find(st, va & kVpnMask, Pid(st));
        return idx != kNoEntry && st->tlb[idx].g != 0u;
    }

    static uint32_t Pid(const cerfengine::MipsCpuState* st) {
        return (st->cp0_entryhi >> kPidShift) & kPidMask;
    }

    /* "the Valid (V) bit ... is not involved in the determination of a matching
       TLB entry" (TMPR3911.pdf PDF p.97, §3.3.6.9). */
    static uint32_t Find(const cerfengine::MipsCpuState* st, uint32_t vpn, uint32_t pid) {
        for (uint32_t i = 0; i < st->nb_tlb; ++i) {
            const cerfengine::MipsTlbEntry& e = st->tlb[i];
            if (e.vpn == vpn && (e.g || e.asid == pid)) return i;
        }
        return kNoEntry;
    }

    static void Replace(cerfengine::MipsTlbContext& ctx, uint32_t idx) {
        cerfengine::MipsCpuState* st = ctx.State();
        cerfengine::MipsTlbEntry& e = st->tlb[idx];
        if (e.v0) ctx.InvalidatePage(e.vpn);
        e.vpn       = st->cp0_entryhi & kVpnMask;
        e.asid      = static_cast<uint16_t>(Pid(st));
        e.page_mask = 0;
        e.g  = static_cast<uint8_t>((st->cp0_entrylo0 & kEntryLoG) != 0);
        e.v0 = static_cast<uint8_t>((st->cp0_entrylo0 & kEntryLoV) != 0);
        e.d0 = static_cast<uint8_t>((st->cp0_entrylo0 & kEntryLoD) != 0);
        e.c0 = static_cast<uint8_t>((st->cp0_entrylo0 & kEntryLoN) != 0);
        e.pfn[0] = st->cp0_entrylo0 & kPfnMask;
        e.v1 = e.d1 = e.c1 = 0;
        e.pfn[1] = 0;

        ctx.InvalidatePage(e.vpn);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(MipsMmuR3000, MipsTlbModel);
