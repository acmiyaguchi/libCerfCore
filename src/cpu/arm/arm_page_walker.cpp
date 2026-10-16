#include "arm_page_walker.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include "../arm_processor_config.h"
#include <cerfcore/cpu/emulated_memory.h>
#include "../physical_address_mapper.h"
#include "arm_cp15_registers.h"
#include "arm_injection_band.h"
#include "arm_mmu_ap_permits.h"
#include "arm_pte.h"

namespace cerfcore {

REGISTER_SERVICE(ArmPageWalker);

bool ArmPageWalker::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::kArm;
}

void ArmPageWalker::OnReady() {
    cp15_             = &emu_.Get<ArmCp15Registers>();
    memory_           = &emu_.Get<EmulatedMemory>();
    processor_config_ = &emu_.Get<ArmProcessorConfig>();
    injection_band_   = &emu_.Get<ArmInjectionBand>();
    address_mapper_   = &emu_.Get<PhysicalAddressMapper>();
}

bool ArmPageWalker::MapPhysicalAddress(ArmPageWalker& self, uint64_t cpu_pa, uint32_t va,
                                       uint32_t domain, cerfengine::ArmMmuAccess access,
                                       uint32_t& system_pa) {
    if (self.address_mapper_->Map(cpu_pa, 1u, system_pa)) return true;
    self.cp15_->RaiseAbort(va, ArmFaultStatus::kExternalAbort, domain, access);
    return false;
}

template <cerfengine::ArmMmuAccess kAccess>
CERF_FORCEINLINE cerfengine::ArmTranslation ArmPageWalker::Walk(uint32_t p, bool is_user_mode,
                                                                uint32_t control_word) {
    ArmPageWalker& self = *this;
    cerfengine::ArmSctlr control{};
    control.word = control_word;
    const ArmCp15File& cp15 = self.cp15_->File();

    /* ARM1136 TRM Table 3-44, c1 bit[23] XP (p. 3-64): "0 = Subpage AP bits
       enabled", "1 = Subpage AP bits disabled (ARMv6 mode)". */
    const bool modern_v6_fmt = self.processor_config_->HasCp15V6() && control.bits.xp;

    /* ARM1136 TRM Table 6-16: with XP=0 a coarse L2 type=3 is a 4 KB
       extended small page (base[31:12], single AP[5:4]). */
    const bool v6_ext_small = self.processor_config_->HasCp15V6() && !control.bits.xp;

    /* ARM DDI 0406C.c Table D15-7: AP=00 access depends on SCTLR.{S,R}. */
    const bool sctlr_s = control.bits.s != 0u;
    const bool sctlr_r = control.bits.r != 0u;

    /* ARM DDI 0406C.c B4.1.43 DACR (p. B4-1558): 0b00 "No access", 0b01 Client "checked
       against the permission bits", 0b10 "Reserved, effect is UNPREDICTABLE", 0b11
       Manager "not checked against the permission bits". */
    const uint32_t dacr = cp15.dacr;
    uint32_t effective_address = 0;

    const uint32_t l1_pa = ArmL1DescriptorAddress(p, cp15.ttbcr, cp15.ttbr0, cp15.ttbr1);
    uint8_t* l1_host = self.memory_->TryTranslateWrite(l1_pa);
    if (!l1_host) {
        self.cp15_->RaiseAbort(p, ArmFaultStatus::kExternalAbortTranslation1, 0u, kAccess);
        return {};
    }
    ArmL1Pte l1_pte;
    l1_pte.word = cerf::le::U32(l1_host);

    struct {
        uint32_t span_bytes    = 0x1000u;
        bool     global        = false;
        bool     fast_fillable = true;
    } new_slot{};

    switch (l1_pte.fault.type) {
    case ArmL1PteType::kFault: {
        uint32_t band_pa = 0;
        if (uint8_t* host = self.injection_band_->Serve(p, kAccess, band_pa)) {
            cerfengine::ArmTranslation band;
            band.mapped = true;
            band.pa     = band_pa;
            band.host   = host;
            return band;
        }
        self.cp15_->RaiseAbort(p, ArmFaultStatus::kTranslationSection, 0u, kAccess);
        return {};
    }

    case ArmL1PteType::kCoarse: {
        const uint32_t domain    = l1_pte.coarse.domain;
        const uint32_t dom_field = (dacr >> (domain << 1)) & 3u;
        const uint32_t l2_pa = (l1_pte.coarse.page_table_base << 10)
                             | (((p >> 12) & 0xFFu) << 2);
        uint8_t* l2_host = self.memory_->TryTranslateWrite(l2_pa);
        if (!l2_host) {
            self.cp15_->RaiseAbort(p, ArmFaultStatus::kExternalAbortTranslation2, domain, kAccess);
            return {};
        }
        ArmL2Pte l2_pte;
        l2_pte.word = cerf::le::U32(l2_host);

        switch (l2_pte.fault.type) {
        case 0:
            self.cp15_->RaiseAbort(p, ArmFaultStatus::kTranslationPage, domain, kAccess);
            return {};
        case 3:
            if (!modern_v6_fmt) {
                if (!v6_ext_small) {
                    self.cp15_->RaiseAbort(p, ArmFaultStatus::kTranslationPage, domain, kAccess);
                    return {};
                }
                /* ARM1136 TRM Table 6-16/Fig 6-5: 4 KB extended small
                   page, single AP at bits[5:4], no nG (global). */
                if (!(dom_field & 1u)) {
                    self.cp15_->RaiseAbort(p, ArmFaultStatus::kDomainPage, domain, kAccess);
                    return {};
                }
                const uint32_t ap = (l2_pte.word >> 4) & 3u;
                if (dom_field == 1u &&
                    !ApPermits<kAccess>(ap, is_user_mode, sctlr_s, sctlr_r)) {
                    self.cp15_->RaiseAbort(p, ArmFaultStatus::kPermissionPage, domain, kAccess);
                    return {};
                }
                new_slot.global   = true;
                effective_address = ArmExtSmallPagePa(l2_pte.word, p);
                break;
            }
            if (!(dom_field & 1u)) {
                self.cp15_->RaiseAbort(p, ArmFaultStatus::kDomainPage, domain, kAccess);
                return {};
            }
            if constexpr (kAccess == cerfengine::ArmMmuAccess::kExecute) {
                self.cp15_->RaiseAbort(p, ArmFaultStatus::kPermissionPage, domain, kAccess);
                return {};
            }
            [[fallthrough]];
        case 2: {
            if (!(dom_field & 1u)) {
                self.cp15_->RaiseAbort(p, ArmFaultStatus::kDomainPage, domain, kAccess);
                return {};
            }
            bool ap_ok;
            if (modern_v6_fmt) {
                const uint32_t ap = ((l2_pte.word >> 4) & 3u) |
                                    (((l2_pte.word >> 9) & 1u) << 2);
                ap_ok = ApPermitsV6<kAccess>(ap, is_user_mode);
            } else {
                /* ARM DDI 0406C.c p. D15-2609 Subpage support: four equal
                   subpages, subpage 0 lowest - a 4 KB Small page's subpage
                   index is VA[11:10]. */
                const uint32_t ap = ArmL2SubpageAp(l2_pte, (p >> 10) & 3u);
                ap_ok = ApPermits<kAccess>(ap, is_user_mode, sctlr_s, sctlr_r);
            }
            if (dom_field == 1u && !ap_ok) {
                self.cp15_->RaiseAbort(p, ArmFaultStatus::kPermissionPage, domain, kAccess);
                return {};
            }
            /* ARM DDI 0406C.c B3.5 Fig B3-5: L2 small page nG at bit[11]. */
            new_slot.global   = !((l2_pte.word >> 11) & 1u);
            effective_address = ArmTranslateSmallPage(l2_pte, p).physical_address;
            break;
        }
        case 1: {
            if (modern_v6_fmt) {
                if (!(dom_field & 1u)) {
                    self.cp15_->RaiseAbort(p, ArmFaultStatus::kDomainPage, domain, kAccess);
                    return {};
                }
                /* ARM DDI 0406C.c Figure B3-5 Large page: XN [15],
                   AP[2] [9], AP[1:0] [5:4], nG [11]. */
                if constexpr (kAccess == cerfengine::ArmMmuAccess::kExecute) {
                    if (l2_pte.large_page.xn) {
                        self.cp15_->RaiseAbort(p, ArmFaultStatus::kPermissionPage, domain,
                                          kAccess);
                        return {};
                    }
                }
                const uint32_t ap = ((l2_pte.word >> 4) & 3u) |
                                    (((l2_pte.word >> 9) & 1u) << 2);
                if (dom_field == 1u && !ApPermitsV6<kAccess>(ap, is_user_mode)) {
                    self.cp15_->RaiseAbort(p, ArmFaultStatus::kPermissionPage, domain, kAccess);
                    return {};
                }
                const ArmL2Translation large = ArmTranslateLargePage(l2_pte, p);
                new_slot.span_bytes = large.span_bytes;
                new_slot.global     = !((l2_pte.word >> 11) & 1u);
                effective_address   = large.physical_address;
                break;
            }
            if (!(dom_field & 1u)) {
                self.cp15_->RaiseAbort(p, ArmFaultStatus::kDomainPage, domain, kAccess);
                return {};
            }
            /* ARM DDI 0406C.c p. D15-2609 Subpage support: four equal
               subpages, subpage 0 lowest - a 64 KB Large page's subpage
               index is VA[15:14]. */
            const uint32_t ap = ArmL2SubpageAp(l2_pte, (p >> 14) & 3u);
            if (dom_field == 1u && !ApPermits<kAccess>(ap, is_user_mode, sctlr_s, sctlr_r)) {
                self.cp15_->RaiseAbort(p, ArmFaultStatus::kPermissionPage, domain, kAccess);
                return {};
            }
            const ArmL2Translation large = ArmTranslateLargePage(l2_pte, p);
            new_slot.span_bytes = large.span_bytes;
            /* ARM DDI 0406C.c B3.5 Fig B3-5: L2 large page nG at bit[11]. */
            new_slot.global     = !((l2_pte.word >> 11) & 1u);
            effective_address   = large.physical_address;
            break;
        }
        }
        if (!ArmPageWalker::MapPhysicalAddress(self, effective_address, p, domain, kAccess,
                                               effective_address)) {
            return {};
        }
        break;
    }

    case ArmL1PteType::kSection: {
        const ArmSupersectionFormat ss_format = self.processor_config_->SupersectionFormat();
        if (ArmSupersectionUnresolved(l1_pte.word, ss_format)) {
            self.emu_.Get<Fatal>().Die("MMU walk: L1 Supersection (bit 18) on a core that declares "
                                  "no Supersection support (va=0x%08X L1_pa=0x%08X "
                                  "L1_pte=0x%08X)", p, l1_pa, l1_pte.word);
        }
        const ArmSectionTranslation translation = ArmTranslateSection(l1_pte.word, p, ss_format);
        const uint32_t domain    = translation.domain;
        const uint32_t dom_field = (dacr >> (domain << 1)) & 3u;
        if (!(dom_field & 1u)) {
            self.cp15_->RaiseAbort(p, ArmFaultStatus::kDomainSection, domain, kAccess);
            return {};
        }
        bool ap_ok;
        uint32_t v7_ap = 0;
        if (modern_v6_fmt) {
            v7_ap = ((l1_pte.word >> 10) & 3u) | (((l1_pte.word >> 15) & 1u) << 2);
            ap_ok = ApPermitsV6<kAccess>(v7_ap, is_user_mode);
        } else {
            ap_ok = ApPermits<kAccess>(l1_pte.section.ap, is_user_mode, sctlr_s, sctlr_r);
        }
        if (dom_field == 1u && !ap_ok) {
            LOG(Caution, "MMU walk: L1 section permission denied "
                    "va=0x%08X L1_pte=0x%08X v7_ap=%u access=%u user=%u\n",
                p, l1_pte.word, v7_ap,
                static_cast<unsigned>(kAccess),
                static_cast<unsigned>(is_user_mode));
            self.cp15_->RaiseAbort(p, ArmFaultStatus::kPermissionSection, domain, kAccess);
            return {};
        }
        if (!ArmPageWalker::MapPhysicalAddress(self, translation.physical_address, p, domain,
                                               kAccess, effective_address)) {
            return {};
        }
        new_slot.span_bytes = translation.span_bytes;
        /* ARM DDI 0406C.c B3.5 Fig B3-4: L1 Section nG at bit[17]. */
        new_slot.global     = !((l1_pte.word >> 17) & 1u);
        break;
    }

    case ArmL1PteType::kFine: {
        /* ARM DDI 0406C.c D12.6 / D15.6.3: the fine second-level page
           table format is not supported from ARMv6, at any SCTLR.XP. */
        if (self.processor_config_->HasCp15V6()) {
            self.emu_.Get<Fatal>().Die("MMU walk: L1 type=3 (Fine) on ARMv6+ (va=0x%08X "
                                  "L1_pa=0x%08X L1_pte=0x%08X)", p, l1_pa, l1_pte.word);
        }
        const uint32_t domain    = l1_pte.fine.domain;
        const uint32_t dom_field = (dacr >> (domain << 1)) & 3u;
        /* ARM DDI 0406C.c Figure D15-1: second-level descriptor PA =
           table base[31:12] | (VA[19:10] << 2). */
        const uint32_t l2_pa = (l1_pte.fine.page_table_base << 12) |
                               (((p >> 10) & 0x3FFu) << 2);
        uint8_t* l2_host = self.memory_->TryTranslateWrite(l2_pa);
        if (!l2_host) {
            self.cp15_->RaiseAbort(p, ArmFaultStatus::kExternalAbortTranslation2, domain, kAccess);
            return {};
        }
        ArmL2Pte l2_pte;
        l2_pte.word = cerf::le::U32(l2_host);

        switch (l2_pte.fault.type) {
        case ArmL2PteType::kFault:
            self.cp15_->RaiseAbort(p, ArmFaultStatus::kTranslationPage, domain, kAccess);
            return {};
        case ArmL2PteType::kLargePage: {
            if (!(dom_field & 1u)) {
                self.cp15_->RaiseAbort(p, ArmFaultStatus::kDomainPage, domain, kAccess);
                return {};
            }
            const uint32_t ap = ArmL2SubpageAp(l2_pte, (p >> 14) & 3u);
            if (dom_field == 1u && !ApPermits<kAccess>(ap, is_user_mode, sctlr_s, sctlr_r)) {
                self.cp15_->RaiseAbort(p, ArmFaultStatus::kPermissionPage, domain, kAccess);
                return {};
            }
            const ArmL2Translation large = ArmTranslateLargePage(l2_pte, p);
            new_slot.span_bytes = large.span_bytes;
            new_slot.global     = true;
            effective_address   = large.physical_address;
            break;
        }
        case ArmL2PteType::kSmallPage: {
            if (!(dom_field & 1u)) {
                self.cp15_->RaiseAbort(p, ArmFaultStatus::kDomainPage, domain, kAccess);
                return {};
            }
            const uint32_t ap = ArmL2SubpageAp(l2_pte, (p >> 10) & 3u);
            if (dom_field == 1u && !ApPermits<kAccess>(ap, is_user_mode, sctlr_s, sctlr_r)) {
                self.cp15_->RaiseAbort(p, ArmFaultStatus::kPermissionPage, domain, kAccess);
                return {};
            }
            new_slot.global   = true;
            effective_address = ArmTranslateSmallPage(l2_pte, p).physical_address;
            break;
        }
        case ArmL2PteType::kTinyPage: {
            if (!(dom_field & 1u)) {
                self.cp15_->RaiseAbort(p, ArmFaultStatus::kDomainPage, domain, kAccess);
                return {};
            }
            if (dom_field == 1u &&
                !ApPermits<kAccess>(l2_pte.tiny_page.ap, is_user_mode, sctlr_s, sctlr_r)) {
                self.cp15_->RaiseAbort(p, ArmFaultStatus::kPermissionPage, domain, kAccess);
                return {};
            }
            const ArmL2Translation tiny = ArmTranslateTinyPage(l2_pte, p);
            new_slot.global        = true;
            effective_address      = tiny.physical_address;
            new_slot.span_bytes    = tiny.span_bytes;
            new_slot.fast_fillable = false;
            break;
        }
        }
        if (!ArmPageWalker::MapPhysicalAddress(self, effective_address, p, domain, kAccess,
                                               effective_address)) {
            return {};
        }
        break;
    }
    }

    cerfengine::ArmTranslation result;
    result.mapped     = true;
    result.pa         = effective_address;
    result.span_bytes = new_slot.span_bytes;
    result.global     = new_slot.global;
    result.cacheable  = new_slot.fast_fillable;
    return result;
}

CERF_FORCEINLINE bool ArmPageWalker::ProbeWalk(uint32_t mva, uint32_t control_word,
                                               uint32_t* pa) {
    ArmPageWalker& self = *this;
    cerfengine::ArmSctlr sctlr{};
    sctlr.word = control_word;
    const ArmCp15File& cp15 = self.cp15_->File();

    const uint32_t l1_pa = ArmL1DescriptorAddress(mva, cp15.ttbcr, cp15.ttbr0, cp15.ttbr1);
    uint8_t* l1_host = self.memory_->TryTranslateWrite(l1_pa);
    if (!l1_host) return false;
    ArmL1Pte l1_pte;
    l1_pte.word = cerf::le::U32(l1_host);

    switch (l1_pte.fault.type) {
    case ArmL1PteType::kSection: {
        const ArmSupersectionFormat ss_format = self.processor_config_->SupersectionFormat();
        if (ArmSupersectionUnresolved(l1_pte.word, ss_format)) return false;
        const ArmSectionTranslation translation = ArmTranslateSection(l1_pte.word, mva, ss_format);
        return self.address_mapper_->Map(translation.physical_address, 1u, *pa);
    }

    case ArmL1PteType::kCoarse: {
        const uint32_t l2_pa = (l1_pte.coarse.page_table_base << 10)
                             | (((mva >> 12) & 0xFFu) << 2);
        uint8_t* l2_host = self.memory_->TryTranslateWrite(l2_pa);
        if (!l2_host) return false;
        ArmL2Pte l2_pte;
        l2_pte.word = cerf::le::U32(l2_host);

        const bool v6_ext_small = self.processor_config_->HasCp15V6() && !sctlr.bits.xp;
        uint32_t cpu_pa = 0;
        if (l2_pte.fault.type == ArmL2PteType::kSmallPage) {
            cpu_pa = ArmTranslateSmallPage(l2_pte, mva).physical_address;
        } else if (l2_pte.fault.type == ArmL2PteType::kExtendedSmallPage && v6_ext_small) {
            cpu_pa = ArmExtSmallPagePa(l2_pte.word, mva);
        } else {
            return false;
        }
        return self.address_mapper_->Map(cpu_pa, 1u, *pa);
    }

    default:
        return false;
    }
}

}

template <cerfengine::ArmMmuAccess kAccess>
cerfengine::ArmTranslation CERFENGINE_FASTCALL cerfengine::host::ArmWalk(void* context,
                                                                         uint32_t mva, bool user,
                                                                         uint32_t control) {
    return static_cast<cerfcore::ArmPageWalker*>(context)->Walk<kAccess>(mva, user, control);
}

bool CERFENGINE_FASTCALL cerfengine::host::ArmProbeWalk(void* context, uint32_t mva,
                                                        uint32_t control, uint32_t* pa) {
    return static_cast<cerfcore::ArmPageWalker*>(context)->ProbeWalk(mva, control, pa);
}

template cerfengine::ArmTranslation CERFENGINE_FASTCALL
cerfengine::host::ArmWalk<cerfengine::ArmMmuAccess::kRead>(void*, uint32_t, bool, uint32_t);
template cerfengine::ArmTranslation CERFENGINE_FASTCALL
cerfengine::host::ArmWalk<cerfengine::ArmMmuAccess::kWrite>(void*, uint32_t, bool, uint32_t);
template cerfengine::ArmTranslation CERFENGINE_FASTCALL
cerfengine::host::ArmWalk<cerfengine::ArmMmuAccess::kReadWrite>(void*, uint32_t, bool, uint32_t);
template cerfengine::ArmTranslation CERFENGINE_FASTCALL
cerfengine::host::ArmWalk<cerfengine::ArmMmuAccess::kExecute>(void*, uint32_t, bool, uint32_t);
