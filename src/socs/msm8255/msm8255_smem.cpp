#include "msm8255_smem.h"

#include "msm8255_ram_partitions.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/boot/guest_cold_boot.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../../cpu/arm/arm_core_clock.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* Linux arch/arm/mach-msm msm_iomap-7x30.h:
   MSM_SHARED_RAM_PHYS 0x00100000, MSM_SHARED_RAM_SIZE SZ_1M. */
constexpr uint32_t kSmemPa   = 0x00100000u;
constexpr uint32_t kSmemSize = 0x00100000u;

constexpr uint32_t kProcCommBytes = 4u * 16u;
constexpr uint32_t kVersionBytes  = 32u * 4u;
constexpr uint32_t kHeapInfoBytes = 4u * 4u;
constexpr uint32_t kHeapTocEntries = 512u;
constexpr uint32_t kHeapTocBytes   = kHeapTocEntries * 16u;
constexpr uint32_t kHeapInfoOff   = kProcCommBytes + kVersionBytes;
constexpr uint32_t kSharedBytes =
    kHeapInfoOff + kHeapInfoBytes + kHeapTocBytes;

/* Linux arch/arm/mach-msm smd_private.h, struct smem_heap_info:
   unsigned initialized; unsigned free_offset; unsigned heap_remaining;
   unsigned reserved. */
constexpr uint32_t kHeapInitializedOff  = kHeapInfoOff + 0u;
constexpr uint32_t kHeapFreeOffsetOff   = kHeapInfoOff + 4u;
constexpr uint32_t kHeapRemainingOff    = kHeapInfoOff + 8u;
constexpr uint32_t kHeapReservedOff     = kHeapInfoOff + 12u;

constexpr uint32_t kFixedTailBytes = 0xF8u;
constexpr uint32_t kFixedAreaEnd   = kSharedBytes + kFixedTailBytes;

constexpr uint32_t kHeapInitialized = 1u;

constexpr uint32_t kHeapTocOff    = kHeapInfoOff + kHeapInfoBytes;
constexpr uint32_t kTocEntryBytes = 16u;

constexpr uint32_t kTocAllocatedOff = 0u;
constexpr uint32_t kTocOffsetOff    = 4u;
constexpr uint32_t kTocSizeOff      = 8u;
constexpr uint32_t kTocAllocated    = 1u;

/* Linux arch/arm/mach-msm smd_private.h: SMEM_CLKREGIM_BSP and
   SMEM_CLKREGIM_SOURCES, evaluated over that enum with
   SMEM_NUM_SMD_CHANNELS 64. */
constexpr uint32_t kIdClkregimBsp     = 336u;
constexpr uint32_t kIdClkregimSources = 337u;

/* Little Kernel platform/msm_shared/smem.h: SMEM_USABLE_RAM_PARTITION_TABLE,
   struct smem_ram_ptable and its two magic words. */
constexpr uint32_t kIdRamPtable  = 402u;
constexpr uint32_t kPtableMagic1 = 0x9DA5E0A8u;
constexpr uint32_t kPtableMagic2 = 0xAF9EC4E2u;

constexpr uint32_t kPtableMagic2Off = 4u;
constexpr uint32_t kPtableLenOff    = 16u;
constexpr uint32_t kPtablePartsOff  = 20u;
constexpr uint32_t kPtableMaxParts  = 32u;
constexpr uint32_t kPtablePartBytes = 56u;
constexpr uint32_t kPtableBytes =
    kPtablePartsOff + kPtableMaxParts * kPtablePartBytes;

constexpr uint32_t kPartStartOff    = 16u;
constexpr uint32_t kPartSizeOff     = 20u;
constexpr uint32_t kPartAttrOff     = 24u;
constexpr uint32_t kPartCategoryOff = 28u;
constexpr uint32_t kPartDomainOff   = 32u;
constexpr uint32_t kPartTypeOff     = 36u;

/* Little Kernel platform/msm_shared/smem.h: SMEM_BOOT_INFO_FOR_APPS, and the
   PLATFORM_MSM7X30 boot_info_for_apps whose boot_flags follows a 72-byte
   boot_symmetric_key_info. */
constexpr uint32_t kIdBootInfo    = 418u;
constexpr uint32_t kBootInfoBytes = 100u;
constexpr uint32_t kBootFlagsOff  = 72u;

constexpr uint32_t kBootFlags = 1u;

constexpr uint32_t kIdDalGlobalCtxt = 404u;
constexpr uint32_t kDalCtxtBytes    = 0x2000u;
constexpr uint32_t kDalArenaBytes   = 4096u;

constexpr uint32_t kDalRecLenOff   = 0x00u;
constexpr uint32_t kDalRecNameOff  = 0x04u;
constexpr uint32_t kDalRecNameMax  = 11u;
constexpr uint32_t kDalRecFlagsOff = 0x18u;

constexpr uint32_t kDalHdrBytes = 32u;
constexpr uint32_t kDalHdrFlags = 0x20000u;
constexpr char     kDalHdrName[] = "dalspinlock";

constexpr uint32_t kDalBusBytes = 48u;
constexpr uint32_t kDalBusFlags = 0x10000u;
constexpr const char* kDalBusNames[] = {"PMIC_SSBI", "CODEC_SSBI"};
constexpr uint32_t kDalBusCount =
    static_cast<uint32_t>(sizeof(kDalBusNames) / sizeof(kDalBusNames[0]));
static_assert(kDalHdrBytes + kDalBusCount * kDalBusBytes <= kDalArenaBytes,
              "the seeded dal records must fit the mapped arena page");

constexpr uint32_t kDalCtxtVersionOff = 0x20u;
constexpr uint32_t kDalCtxtPoweredOff = 0x24u;
constexpr uint32_t kDalCtxtVersion    = 2u;
constexpr uint32_t kDalCtxtPowered    = 1u;

constexpr uint32_t kBspBytes  = 20456u;
constexpr uint32_t kSrcBytes  = 208u;
constexpr uint32_t kBspMagic  = 0xCCEE0003u;
constexpr uint32_t kSrcMagic  = 0xCCEE0002u;

constexpr uint32_t kBspRecordsOff  = 13168u;
constexpr uint32_t kBspRecordBytes = 52u;
constexpr uint32_t kBspRecordIndex = 1u;

constexpr uint32_t kRecKeySrcOff   = 4u;
constexpr uint32_t kRecKeyLevelOff = 8u;

constexpr uint32_t kRecKeySrc   = 9u;
constexpr uint32_t kRecKeyLevel = 1u;

constexpr uint32_t kRecVddMvOff    = 40u;
constexpr uint32_t kRecAvsdscrOff  = 48u;

constexpr uint32_t kBspPerfLevelsOff   = 13104u;
constexpr uint32_t kBspPerfLevelCount  = 8u;
constexpr uint32_t kBspPerfLevelStride = 8u;

constexpr uint32_t kVddMv = 1250u;

/* Linux arch/arm/mach-msm avs.c AVSDSCR_INPUT, written by avs_reset_delays;
   avs_hw.S encodes that register as mcr p15, 7, Rd, c15, c0, 6. */
constexpr uint32_t kAvsdscr = 0x01004860u;

/* Linux arch/arm/mach-msm avs_hw.S avs_reset_delays: AVSCSR 0x61 enables the
   CPU, V and L2 AVS modules, encoded as mcr p15, 7, Rd, c15, c1, 7. */
constexpr uint32_t kBspAvscsrOff = 15392u;
constexpr uint32_t kAvscsr       = 0x61u;

constexpr uint32_t kBspSawCfgOff = 15380u;
constexpr uint32_t kSawCfgSeed   = 6u;

constexpr uint32_t Align8(uint32_t v) { return (v + 7u) & ~7u; }

constexpr uint32_t kBspOff = Align8(kFixedAreaEnd);
constexpr uint32_t kSrcOff = Align8(kBspOff + kBspBytes);
constexpr uint32_t kPtableOff = Align8(kSrcOff + kSrcBytes);
constexpr uint32_t kBootInfoOff = Align8(kPtableOff + kPtableBytes);
constexpr uint32_t kDalCtxtOff  = Align8(kBootInfoOff + kBootInfoBytes);
constexpr uint32_t kDalArenaPad =
    (kDalArenaBytes - ((kSmemPa + kDalCtxtOff) & (kDalArenaBytes - 1u)))
    & (kDalArenaBytes - 1u);
constexpr uint32_t kDalArenaOff = kDalCtxtOff + kDalArenaPad;
constexpr uint32_t kHeapUsedEnd = Align8(kDalCtxtOff + kDalCtxtBytes);
static_assert(kHeapUsedEnd <= kSmemSize,
              "the published smem items must fit the shared window");

}

bool Msm8255Smem::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

void Msm8255Smem::OnReady() {
    Seed();
    emu_.Get<GuestColdBoot>().RegisterReplay([this] { Seed(); });
}

uint32_t Msm8255Smem::SmemPa() { return kSmemPa; }

/* Linux arch/arm/mach-msm smd.c smem_find: the item pointer is handed out only
   when the caller's byte count, rounded up to 8, equals the allocated size. */
uint32_t Msm8255Smem::ItemPa(uint32_t id, uint32_t bytes) {
    uint32_t off  = 0u;
    uint32_t size = 0u;
    if (!ReadTocEntry(id, off, size)) {
        return 0u;
    }
    const uint32_t want = Align8(bytes);
    if (size != want) {
        emu_.Get<Fatal>().Die(
            "msm8255 smem: item %u holds %u bytes, and the modem peer models it "
            "as %u", id, size, want);
    }
    return kSmemPa + off;
}

/* Linux arch/arm/mach-msm smd.c smem_item: the caller receives the item's own
   recorded size, in place of declaring the extent it expects. */
bool Msm8255Smem::ItemPaAndSize(uint32_t id, uint32_t& pa, uint32_t& bytes) {
    uint32_t off  = 0u;
    uint32_t size = 0u;
    if (!ReadTocEntry(id, off, size)) {
        return false;
    }
    pa    = kSmemPa + off;
    bytes = size;
    return true;
}

bool Msm8255Smem::ReadTocEntry(uint32_t id, uint32_t& off, uint32_t& size) {
    auto& mem = emu_.Get<EmulatedMemory>();
    const uint32_t toc = TocEntryPa(id);
    if (mem.ReadWord(toc + kTocAllocatedOff) != kTocAllocated) {
        return false;
    }
    off  = mem.ReadWord(toc + kTocOffsetOff);
    size = mem.ReadWord(toc + kTocSizeOff);
    if (off >= kSmemSize || size > kSmemSize - off) {
        emu_.Get<Fatal>().Die(
            "msm8255 smem: item %u claims offset 0x%X and size %u, which leaves "
            "the %u-byte shared window", id, off, size, kSmemSize);
    }
    return true;
}

uint32_t Msm8255Smem::TocEntryPa(uint32_t id) {
    if (id >= kHeapTocEntries) {
        emu_.Get<Fatal>().Die(
            "msm8255 smem: item id %u is outside the %u-entry heap toc",
            id, kHeapTocEntries);
    }
    return kSmemPa + kHeapTocOff + kTocEntryBytes * id;
}

void Msm8255Smem::Seed() {
    auto& mem = emu_.Get<EmulatedMemory>();
    mem.WriteWord(kSmemPa + kHeapInitializedOff, kHeapInitialized);
    mem.WriteWord(kSmemPa + kHeapFreeOffsetOff,  kHeapUsedEnd);
    mem.WriteWord(kSmemPa + kHeapRemainingOff,   kSmemSize - kHeapUsedEnd);
    mem.WriteWord(kSmemPa + kHeapReservedOff,    0u);

    PublishItem(kIdClkregimBsp,     kBspOff, kBspBytes, kBspMagic);
    PublishItem(kIdClkregimSources, kSrcOff, kSrcBytes, kSrcMagic);
    PublishRamPartitions();

    PublishItem(kIdBootInfo, kBootInfoOff, kBootInfoBytes, 0u);
    mem.WriteWord(kSmemPa + kBootInfoOff + kBootFlagsOff, kBootFlags);

    SeedDalGlobalContext();

    SeedSpeedRecord();
    SeedPerfLevels();
    SeedAvsConfig();
}

uint32_t Msm8255Smem::RecordPa(uint32_t index) const {
    return kSmemPa + kBspOff + kBspRecordsOff + kBspRecordBytes * index;
}

void Msm8255Smem::SeedSpeedRecord() {
    auto& mem = emu_.Get<EmulatedMemory>();
    const uint32_t rec = RecordPa(kBspRecordIndex);
    mem.WriteWord(rec + 0u, static_cast<uint32_t>(
                                emu_.Get<ArmCoreClock>().ResetRate().RoundedHz()));
    mem.WriteWord(rec + kRecKeySrcOff,     kRecKeySrc);
    mem.WriteWord(rec + kRecKeyLevelOff,   kRecKeyLevel);
    mem.WriteWord(rec + kRecVddMvOff,      kVddMv);
    mem.WriteWord(rec + kRecAvsdscrOff,    kAvsdscr);
}

void Msm8255Smem::SeedPerfLevels() {
    auto& mem = emu_.Get<EmulatedMemory>();
    const uint32_t base = kSmemPa + kBspOff + kBspPerfLevelsOff;
    for (uint32_t i = 0; i < kBspPerfLevelCount; ++i) {
        mem.WriteWord(base + kBspPerfLevelStride * i, kBspRecordIndex);
    }
}

void Msm8255Smem::SeedAvsConfig() {
    auto& mem = emu_.Get<EmulatedMemory>();
    mem.WriteWord(kSmemPa + kBspOff + kBspAvscsrOff, kAvscsr);
    mem.WriteWord(kSmemPa + kBspOff + kBspSawCfgOff, kSawCfgSeed);
}

void Msm8255Smem::WriteDalRecordName(uint32_t rec, const char* name) {
    auto& mem = emu_.Get<EmulatedMemory>();
    for (uint32_t i = 0; i < kDalRecNameMax && name[i] != '\0'; ++i) {
        mem.WriteByte(rec + kDalRecNameOff + i,
                      static_cast<uint8_t>(name[i]));
    }
}

void Msm8255Smem::SeedDalGlobalContext() {
    PublishItem(kIdDalGlobalCtxt, kDalCtxtOff, kDalCtxtBytes, 0u);

    auto& mem = emu_.Get<EmulatedMemory>();

    mem.WriteWord(kSmemPa + kDalCtxtOff, kDalArenaPad);

    const uint32_t hdr = kSmemPa + kDalArenaOff;
    mem.WriteWord(hdr + kDalRecLenOff, kDalHdrBytes);
    WriteDalRecordName(hdr, kDalHdrName);
    mem.WriteWord(hdr + kDalRecFlagsOff, kDalHdrFlags);

    uint32_t rec = hdr + kDalHdrBytes;
    for (uint32_t i = 0; i < kDalBusCount; ++i) {
        mem.WriteWord(rec + kDalRecLenOff, kDalBusBytes);
        WriteDalRecordName(rec, kDalBusNames[i]);
        mem.WriteWord(rec + kDalRecFlagsOff,    kDalBusFlags);
        mem.WriteWord(rec + kDalCtxtVersionOff, kDalCtxtVersion);
        mem.WriteWord(rec + kDalCtxtPoweredOff, kDalCtxtPowered);
        rec += kDalBusBytes;
    }
}

void Msm8255Smem::PublishRamPartitions() {
    auto* board = emu_.TryGet<Msm8255RamPartitions>();
    if (!board) {
        return;
    }

    const uint32_t count = board->PartitionCount();
    if (count > kPtableMaxParts) {
        emu_.Get<Fatal>().Die(
            "msm8255 smem: the board declares %u ram partitions and the "
            "partition table carries %u", count, kPtableMaxParts);
    }

    PublishItem(kIdRamPtable, kPtableOff, kPtableBytes, kPtableMagic1);

    auto& mem = emu_.Get<EmulatedMemory>();
    const uint32_t base = kSmemPa + kPtableOff;
    mem.WriteWord(base + kPtableMagic2Off, kPtableMagic2);
    mem.WriteWord(base + kPtableLenOff,    count);
    for (uint32_t i = 0; i < count; ++i) {
        const Msm8255RamPartition part = board->Partition(i);
        const uint32_t rec = base + kPtablePartsOff + kPtablePartBytes * i;
        mem.WriteWord(rec + kPartStartOff,    part.start);
        mem.WriteWord(rec + kPartSizeOff,     part.size);
        mem.WriteWord(rec + kPartAttrOff,     part.attr);
        mem.WriteWord(rec + kPartCategoryOff, part.category);
        mem.WriteWord(rec + kPartDomainOff,   part.domain);
        mem.WriteWord(rec + kPartTypeOff,     part.type);
    }
}

void Msm8255Smem::PublishItem(uint32_t id, uint32_t off, uint32_t size,
                              uint32_t magic) {
    auto& mem = emu_.Get<EmulatedMemory>();
    const uint32_t toc = TocEntryPa(id);
    mem.WriteWord(toc +  0u, 1u);
    mem.WriteWord(toc +  4u, off);
    mem.WriteWord(toc +  8u, Align8(size));
    mem.WriteWord(toc + 12u, 0u);
    mem.WriteWord(kSmemPa + off, magic);
}

REGISTER_SERVICE(Msm8255Smem);

}
