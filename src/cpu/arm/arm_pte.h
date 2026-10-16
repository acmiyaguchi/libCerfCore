#pragma once

#include "../arm_processor_config.h"

#include <cstdint>

namespace cerfcore {

namespace ArmL1PteType {
    constexpr uint32_t kFault   = 0;
    constexpr uint32_t kCoarse  = 1;
    constexpr uint32_t kSection = 2;
    constexpr uint32_t kFine    = 3;
}

namespace ArmL2PteType {
    constexpr uint32_t kFault             = 0;
    constexpr uint32_t kLargePage         = 1;
    constexpr uint32_t kSmallPage         = 2;
    constexpr uint32_t kExtendedSmallPage = 3;   /* coarse table: 4 KB
                                                    (ARM DDI 0406C.c Table D15-9) */
    constexpr uint32_t kTinyPage          = 3;   /* fine table: 1 KB
                                                    (ARM DDI 0406C.c Table D15-10) */
}

union ArmL1Pte {
    uint32_t word;

    struct {
        uint32_t type : 2;
        uint32_t sbz  : 30;
    } fault;

    struct {
        uint32_t type             : 2;
        uint32_t reserved1        : 3;
        uint32_t domain           : 4;
        uint32_t p                : 1;
        uint32_t page_table_base  : 22;
    } coarse;

    /* 1 MB Section (ARM DDI 0406C.c Figure B3-4; the ARMv4/v5 layout in
       Table D15-8 leaves [4] and [19:15] SBZ). */
    struct {
        uint32_t pxn          : 1;
        uint32_t one          : 1;
        uint32_t b            : 1;
        uint32_t c            : 1;
        uint32_t xn           : 1;
        uint32_t domain       : 4;
        uint32_t p            : 1;
        uint32_t ap           : 2;
        uint32_t tex          : 3;
        uint32_t ap2          : 1;
        uint32_t s            : 1;
        uint32_t ng           : 1;
        uint32_t zero_18      : 1;
        uint32_t ns           : 1;
        uint32_t section_base : 12;
    } section;

    /* L1 Fine page table descriptor (ARM DDI 0406C.c Table D15-8:
       bits[31:12] give the second-level table's physical address;
       fine tables are 4 KB aligned). */
    struct {
        uint32_t type             : 2;
        uint32_t sbz_4_2          : 3;
        uint32_t domain           : 4;
        uint32_t p                : 1;
        uint32_t sbz_11_10        : 2;
        uint32_t page_table_base  : 20;
    } fine;
};
static_assert(sizeof(ArmL1Pte) == 4, "L1 PTE must be 32 bits");

union ArmL2Pte {
    uint32_t word;

    struct {
        uint32_t type : 2;
        uint32_t sbz  : 30;
    } fault;

    struct {
        uint32_t type             : 2;
        uint32_t b                : 1;
        uint32_t c                : 1;
        uint32_t ap0              : 2;
        uint32_t ap1              : 2;
        uint32_t ap2              : 2;
        uint32_t ap3              : 2;
        uint32_t tex              : 3;
        uint32_t xn               : 1;
        uint32_t large_page_base  : 16;
    } large_page;

    /* 4 KB Small page (ARM DDI 0406C.c Table D15-9): AP0 [5:4], AP1 [7:6],
       AP2 [9:8], AP3 [11:10] - the AP bits for subpages 0..3, subpage 0
       lowest-addressed. */
    struct {
        uint32_t type             : 2;
        uint32_t b                : 1;
        uint32_t c                : 1;
        uint32_t ap0              : 2;
        uint32_t ap1              : 2;
        uint32_t ap2              : 2;
        uint32_t ap3              : 2;
        uint32_t small_page_base  : 20;
    } small_page;

    /* 1 KB Tiny page (ARM DDI 0406C.c Table D15-10): single AP at [5:4],
       SBZ [9:6], base PA[31:10]. */
    struct {
        uint32_t type           : 2;
        uint32_t b              : 1;
        uint32_t c              : 1;
        uint32_t ap             : 2;
        uint32_t sbz_9_6        : 4;
        uint32_t tiny_page_base : 22;
    } tiny_page;
};
static_assert(sizeof(ArmL2Pte) == 4, "L2 PTE must be 32 bits");

struct ArmL2Translation {
    uint32_t physical_address;
    uint32_t span_bytes;
};

inline ArmL2Translation ArmTranslateLargePage(const ArmL2Pte& pte, uint32_t va) {
    return {(pte.large_page.large_page_base << 16) | (va & 0xFFFFu), 0x10000u};
}

inline ArmL2Translation ArmTranslateSmallPage(const ArmL2Pte& pte, uint32_t va) {
    return {(pte.small_page.small_page_base << 12) | (va & 0x0FFFu), 0x1000u};
}

inline ArmL2Translation ArmTranslateTinyPage(const ArmL2Pte& pte, uint32_t va) {
    return {(pte.tiny_page.tiny_page_base << 10) | (va & 0x03FFu), 0x400u};
}

inline bool ArmSupersectionSelected(uint32_t pte_word, ArmSupersectionFormat format) {
    return ((pte_word >> 18) & 1u) != 0u &&
           (format == ArmSupersectionFormat::kPa32 ||
            format == ArmSupersectionFormat::kPa36);
}

inline bool ArmSupersectionUnresolved(uint32_t pte_word,
                                      ArmSupersectionFormat format) {
    return ((pte_word >> 18) & 1u) != 0u &&
           format == ArmSupersectionFormat::kUnknown;
}

struct ArmSectionTranslation {
    uint64_t physical_address;
    uint32_t span_bytes;
    uint32_t domain;
};

/* ARM DDI 0406C.c B3.5.4 and B4.1.154: TTBR selection and L1 index. */
inline uint32_t ArmL1DescriptorAddress(uint32_t va, uint32_t ttbcr,
                                       uint32_t ttbr0, uint32_t ttbr1) {
    const uint32_t n = ttbcr & 7u;
    const uint32_t ttbr0_mask = ~((1u << (14u - n)) - 1u);
    const bool use_ttbr1 = n != 0u && (va >> (32u - n)) != 0u;
    const uint32_t base = use_ttbr1 ? (ttbr1 & 0xFFFFC000u)
                                    : (ttbr0 & ttbr0_mask);
    return base | ((va >> 20) << 2);
}

/* ARM DDI 0406C.c B3.5 Figure B3-4 and "Memory described by Supersections is
   in domain 0"; Intel 316283-002US section 3.2.2.1 Figure 2 for PA[35:32]. */
inline ArmSectionTranslation ArmTranslateSection(uint32_t pte_word, uint32_t va,
                                                 ArmSupersectionFormat format) {
    if (ArmSupersectionSelected(pte_word, format)) {
        uint64_t pa = (pte_word & 0xFF000000u) | (va & 0x00FFFFFFu);
        if (format == ArmSupersectionFormat::kPa36) {
            pa |= static_cast<uint64_t>(pte_word & 0x00F00000u) << 12;
        }
        return {pa, 0x01000000u, 0u};
    }
    return {static_cast<uint64_t>((pte_word & 0xFFF00000u) |
                                  (va & 0x000FFFFFu)),
            0x00100000u,
            (pte_word >> 5) & 0x0Fu};
}

/* ARM1136 TRM Table 6-16 / Fig 6-5: 4 KB extended small page (coarse-L2
   type=3 with SCTLR.XP=0) - PA = base[31:12] | VA[11:0]. */
inline uint32_t ArmExtSmallPagePa(uint32_t pte_word, uint32_t va) {
    return (pte_word & 0xFFFFF000u) | (va & 0x0FFFu);
}

/* ARM DDI 0406C.c Tables D15-9/D15-10 place AP0..AP3 at [5:4]..[11:10];
   p. D15-2609 "Subpage support": APn is the AP field for subpage n of
   four equal subpages, subpage 0 lowest-addressed. */
inline uint32_t ArmL2SubpageAp(const ArmL2Pte& pte, uint32_t subpage) {
    switch (subpage & 3u) {
        case 0:  return pte.small_page.ap0;
        case 1:  return pte.small_page.ap1;
        case 2:  return pte.small_page.ap2;
        default: return pte.small_page.ap3;
    }
}

}
