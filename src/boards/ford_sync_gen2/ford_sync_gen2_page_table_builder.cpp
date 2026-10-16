#include "../page_table_builder.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "ford_sync_gen2_family_id.h"

#include <cstdint>
#include <vector>

namespace {

constexpr uint32_t MB(uint32_t mb) { return mb * 0x100000u; }

enum class OatKind { Dram, Flash, Mmio };

struct OatEntry {
    uint32_t va_base;
    uint32_t pa_base;
    uint32_t size;
    OatKind  kind;
};

/* Ford Sync Gen2 OAL OEMAddressTable, decoded byte-exact from nk.exe 0x801014AC;
   PA region identities per i.MX51 RM (MCIMX51RM Rev.1) Table 2-1. CSD1 is 243 MB,
   not 256 MB: widening it makes VaToPa's first match shadow the peripheral
   windows that reuse the top of its VA range below. */
constexpr OatEntry kOat[] = {
    { 0x80000000u, 0x90000000u, MB(256), OatKind::Dram }, /* CSD0 DDR (NK image lives here) */
    { 0x90000000u, 0xA0000000u, MB(243), OatKind::Dram }, /* CSD1 DDR */
    { 0x9F300000u, 0x5E000000u, MB(1),   OatKind::Mmio }, /* IPUv3 (within IPUEX) */
    { 0x9F400000u, 0xC0000000u, MB(1),   OatKind::Mmio }, /* EMI/WEIM chip-select */
    { 0x9F500000u, 0xCFF00000u, MB(5),   OatKind::Mmio }, /* EMI/WEIM CS + NAND internal buffer */
    { 0x9FA00000u, 0x1FF00000u, MB(1),   OatKind::Dram }, /* SCC internal RAM (0x1FFE0000) */
    { 0x9FB00000u, 0xE0000000u, MB(1),   OatKind::Mmio }, /* TZIC interrupt controller */
    { 0x9FC00000u, 0x00000000u, MB(1),   OatKind::Mmio }, /* i.MX51 boot ROM */
    { 0x9FD00000u, 0x70000000u, MB(1),   OatKind::Mmio }, /* SPBA / AIPS-1 peripherals */
    { 0x9FE00000u, 0x73F00000u, MB(1),   OatKind::Mmio }, /* AIPS-1 (CCM, IOMUXC, ...) */
    { 0x9FF00000u, 0x83F00000u, MB(1),   OatKind::Mmio }, /* AIPS-2 (SRC, DPLLs, ...) */
};

/* CSD0 is the primary bank (kernel imagebase 0x80100000 maps to PA 0x90100000);
   the bootloader-handoff SP is the top of that bank. */
constexpr uint32_t kCsd0PaBase     = 0x90000000u;
constexpr uint32_t kCsd0Size       = MB(256);
constexpr uint32_t kInitStackTopPa = kCsd0PaBase + kCsd0Size;

class FordSyncGen2PageTableBuilder : public PageTableBuilder {
public:
    using PageTableBuilder::PageTableBuilder;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::FordSyncGen2;
    }

    uint32_t InitStackTopPa() const override { return kInitStackTopPa; }
    uint32_t VaToPa(uint32_t va) const override;
    std::vector<DramRegion>   CachedDramRegions()   const override;
    std::vector<BackedRegion> BackedMemoryRegions() const override;
    std::vector<DramRegion>   MappedVaSpans()       const override;
};

uint32_t FordSyncGen2PageTableBuilder::VaToPa(uint32_t va) const {
    for (const auto& e : kOat) {
        if (va >= e.va_base && va < e.va_base + e.size) {
            return e.pa_base + (va - e.va_base);
        }
    }
    LOG(Caution, "FordSyncGen2PageTableBuilder::VaToPa: VA 0x%08X is outside "
            "every i.MX51 OAL OAT band (nk.exe 0x801014AC)\n", va);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

std::vector<DramRegion> FordSyncGen2PageTableBuilder::CachedDramRegions() const {
    std::vector<DramRegion> regions;
    for (const auto& e : kOat) {
        if (e.kind == OatKind::Dram) {
            regions.push_back({ e.va_base, e.pa_base, e.size });
        }
    }
    return regions;
}

std::vector<BackedRegion>
FordSyncGen2PageTableBuilder::BackedMemoryRegions() const {
    std::vector<BackedRegion> regions;
    for (const auto& e : kOat) {
        if (e.kind == OatKind::Mmio) continue;
        const MemoryKind kind =
            (e.kind == OatKind::Flash) ? MemoryKind::kFlash : MemoryKind::kRam;
        regions.push_back({ e.va_base, e.pa_base, e.size, kind });
    }
    /* NFC internal-RAM main buffers (RM Table 45-7, 8x512 B = 4 KB): page 0 is
       backed RAM (mask ROM stages the first NAND page here + the stub executes
       in place). Page 1 (spare + AXI registers, 0xCFFF1000) stays unbacked so it
       reaches Imx51NfcAxiWindow. */
    regions.push_back({ 0x9F5F0000u, 0xCFFF0000u, 0x1000u, MemoryKind::kRam });
    /* CSD1 is physically 256 MB; the OAT caches only its low 243 MB (VA above
       0x9F300000 is reused for MMIO). The HW_REV>=10 graphics reserve (ipuv3_base
       sub_C0A41460: PA 0xAEC00000+20 MB, reached via MmMapIoSpace) needs its tail
       past the cached CSD1 backed as DRAM, else the IPU/GPU EMEM access faults. */
    regions.push_back({ 0xAF300000u, 0xAF300000u, MB(13), MemoryKind::kRam });
    return regions;
}

std::vector<DramRegion> FordSyncGen2PageTableBuilder::MappedVaSpans() const {
    std::vector<DramRegion> regions;
    for (const auto& e : kOat) {
        regions.push_back({ e.va_base, e.pa_base, e.size });
    }
    return regions;
}

}  /* namespace */

REGISTER_SERVICE_AS(FordSyncGen2PageTableBuilder, PageTableBuilder);
