#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace cerfcore {

/* The `.sec` packs partitions by size, but the device NAND holds them
   block-aligned at cumulative 512 KB StartBlocks - a linear `.sec` map feeds the
   stub's block-1 scan the wrong bytes. Placement = the flasher's own algorithm
   (staged_80040000.bin sub_800593c0 + DPS_Write 0x800582FC), verified vs `.sec`. */
class Imx51NandLayout : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    /* Physical NAND byte offset -> `.sec` flash byte offset. nullopt = a blank
       (erased, 0xFF) region: past a module's data, or an unmapped block. */
    std::optional<uint64_t> PhysToSec(uint64_t phys_off) const;

    /* RAW (pre-NFC-BBI-swap) factory page for the nand.img seed: boot region from
       the `.sec`, plus the synthesized BBT/DPS. Returns false for the OS region
       (left erased 0xFF for the guest flasher) - returning true there would pre-fill
       the OS so the IPL boots a stale image and the flasher never runs. */
    bool BuildFactoryPage(uint64_t phys_off, uint8_t* main, size_t main_len,
                          uint8_t* spare, size_t spare_len) const;

    uint64_t OsRegionStartBlock() const { return os_sig_block_; }
    uint64_t DeviceBlocks() const { return device_blocks_; }

    /* DPS manifest SBOOT reads from the tail blocks (DPS_Read 0x8FF0DFBC); the
       `.sec` carries none. Synthesize it new-format - else SBOOT reads it as old
       and issues DPS_Write to upgrade, which the read-only `.sec` NAND cannot
       service (boot wedges). */
    bool IsDpsOffset(uint64_t phys_off) const;
    void BuildDpsPage(uint64_t phys_off, uint8_t* main, size_t main_len,
                      uint8_t* spare, size_t spare_len) const;

    /* SBOOT's DPS_ReadBadBlockTable (0x8FF0D7D0) scans the tail blocks for a BBT
       (sig 0xB041D283); finding none it ERASES one - which the read-only `.sec`
       NAND cannot service. Synthesize an empty (no-bad-block) BBT to avoid that. */
    bool IsBbtBlock(uint64_t phys_off) const;
    void BuildBbtPage(uint64_t phys_off, uint8_t* main, size_t main_len,
                      uint8_t* spare, size_t spare_len) const;

private:
    struct Part {
        uint32_t id;
        uint64_t start_block;
        uint64_t nblocks;
        uint64_t sec_off;
        uint64_t size;          /* bytes of real data in the module (rest of its
                                   blocks read blank) */
    };

    std::vector<Part> parts_;
    uint64_t          device_blocks_ = 0;

    uint64_t os_sig_block_ = 0;   /* id-6 StartBlock = OS-region start          */
    uint64_t nk_sec_off_   = 0;   /* id-7 NK `.sec` offset (ECEC at +0x40)      */
    uint64_t nk_blocks_    = 0;   /* id-7 NK block span                         */

    /* 512 KB block (flash-0x0 config header +0x58; MCIMX51RM Table 9-3, S10). */
    static constexpr uint64_t kBlock = 0x80000u;
    /* 2 GB Micron part (NFC READ ID 0x2C/0x48) -> 4096 blocks. */
    static constexpr uint64_t kDeviceBytes = 0x80000000u;
    /* config + first-stage IPL region; the first XIP module (SBOOT) starts at
       `.sec` 0x5000 (image base 0x4000, flash_header @0x4400, S5). */
    static constexpr uint64_t kBootRegion = 0x5000u;
};

}
