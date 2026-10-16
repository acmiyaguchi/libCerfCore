#include "compactflash_fat32.h"

#include "compactflash_fat_common.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../core/host_file_bytes.h"
#include <cerfcore/core/log.h>
#include "../../storage/fat_volume_layout.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace cerfcore {

REGISTER_SERVICE(CompactFlashFat32Builder);

namespace {

constexpr uint32_t kBytesPerSec = 512;
constexpr uint32_t kSecPerClus  = 1;       /* 512-byte clusters */
constexpr uint32_t kReserved    = 32;      /* FAT32 reserved sectors */
constexpr uint32_t kNumFats     = 2;
constexpr uint32_t kRootClus    = 2;
constexpr uint32_t kMinClusters = 66000;   /* > 65525 -> valid FAT32 */
constexpr uint32_t kEoc         = 0x0FFFFFFFu;

}  /* namespace */

bool CompactFlashFat32Builder::Build(const std::string& out_path,
                                     const std::vector<std::string>& files,
                                     uint32_t data_mb) {
    std::vector<cf_fat::FileEntry> entries =
        cf_fat::CollectEntries(files, kBytesPerSec * kSecPerClus);
    uint32_t root_dir_entries = 1;          /* volume-label entry */
    uint32_t payload_clusters = 0;
    for (const auto& e : entries) {
        root_dir_entries += e.lfn_count + 1;
        payload_clusters += e.clusters;
    }

    const uint32_t root_clusters =
        std::max<uint32_t>(1, (root_dir_entries * 32 + 511) / 512);
    const uint32_t used = root_clusters + payload_clusters + 16;
    /* Requested capacity in clusters: data_mb MiB at kBytesPerSec*kSecPerClus
       per cluster. The card is floored to the larger of the request, the
       file payload, and the FAT32 minimum cluster count. */
    const uint32_t req_clusters = static_cast<uint32_t>(
        (static_cast<uint64_t>(data_mb) * 1024u * 1024u) /
        (kBytesPerSec * kSecPerClus));
    const uint32_t data_clusters =
        std::max(std::max(used, req_clusters), kMinClusters);
    const uint32_t fat_entries = data_clusters + 2;
    const uint32_t fat_sectors = (fat_entries * 4 + 511) / 512;
    const uint32_t total_sectors = kReserved + kNumFats * fat_sectors + data_clusters;

    std::vector<uint8_t> img(static_cast<std::size_t>(total_sectors) * 512, 0);

    uint8_t* bs = img.data();
    fat_volume_layout::Bpb bpb;
    bpb.jump[0] = 0xEB; bpb.jump[1] = 0x58; bpb.jump[2] = 0x90;
    bpb.oem_name = "MSWIN4.1";
    bpb.bytes_per_sector = kBytesPerSec;
    bpb.sectors_per_cluster = kSecPerClus;
    bpb.reserved_sectors = kReserved;
    bpb.num_fats = kNumFats;
    bpb.media = 0xF8;
    bpb.sectors_per_track = 63;
    bpb.num_heads = 16;
    bpb.total_sectors32 = total_sectors;
    fat_volume_layout::WriteBpb(bs, bpb);
    fat_volume_layout::Fat32Extension ext;
    ext.fat_size32 = fat_sectors;
    ext.root_cluster = kRootClus;
    ext.fsinfo_sector = 1;
    ext.backup_boot_sector = 6;
    ext.drive_number = 0x80;
    ext.boot_signature = 0x29;
    ext.volume_id = 0xCE5FCF01u;
    ext.volume_label = "CERF CF    ";
    ext.fs_type = "FAT32   ";
    fat_volume_layout::WriteFat32Extension(bs, ext);
    fat_volume_layout::WriteBootSignature(bs);

    fat_volume_layout::WriteFsInfo(img.data() + 512,
                                   data_clusters - (root_clusters + payload_clusters),
                                   root_clusters + payload_clusters + 2);
    std::memcpy(img.data() + 6 * 512, bs, 512);

    /* Assign clusters: root first (clusters 2..), then each file. */
    uint32_t next = kRootClus + root_clusters;
    for (auto& e : entries) {
        e.first_clus = e.clusters ? next : 0;
        next += e.clusters;
    }

    /* FAT entries. */
    const uint32_t fat0 = kReserved * 512;
    auto set_fat = [&](uint32_t clus, uint32_t val) {
        cerf::le::Put32(img.data() + fat0 + clus * 4, val);
        cerf::le::Put32(img.data() + fat0 + fat_sectors * 512 + clus * 4, val);
    };
    set_fat(0, 0x0FFFFFF8u);
    set_fat(1, kEoc);
    for (uint32_t c = 0; c < root_clusters; ++c)
        set_fat(kRootClus + c, c + 1 < root_clusters ? kRootClus + c + 1 : kEoc);
    for (const auto& e : entries) {
        for (uint32_t c = 0; c < e.clusters; ++c)
            set_fat(e.first_clus + c,
                    c + 1 < e.clusters ? e.first_clus + c + 1 : kEoc);
    }

    /* Data region. */
    const uint32_t data_start = (kReserved + kNumFats * fat_sectors) * 512;
    auto clus_off = [&](uint32_t clus) {
        return data_start + (clus - kRootClus) * kSecPerClus * 512;
    };

    /* Root directory: volume-label entry, then per-file LFN run + SFN. */
    uint8_t* dir = img.data() + clus_off(kRootClus);
    std::memcpy(dir, "CERF CF    ", 11);
    dir[11] = 0x08;            /* ATTR_VOLUME_ID */
    dir += 32;
    for (const auto& e : entries)
        dir = cf_fat::EmitFileDir(dir, e.name, e.sfn, e.lfn_count, e.ntres, e.first_clus,
                                  static_cast<uint32_t>(e.data.size()));

    /* File payloads. */
    for (const auto& e : entries) {
        if (e.data.empty()) continue;
        std::memcpy(img.data() + clus_off(e.first_clus), e.data.data(), e.data.size());
    }

    const HostWriteResult written = WriteHostFileBytes(out_path, img.data(), img.size());
    if (written == HostWriteResult::kOpenFailed) {
        LOG(Caution, "[CF] FAT32 build: cannot open output image for write\n");
        return false;
    }
    const bool ok = written == HostWriteResult::kOk;
    if (!ok) LOG(Caution, "[CF] FAT32 build: short write to image\n");
    else LOG(Cerf, "[CF] FAT32 image built: %u sectors, %zu file(s)\n",
             total_sectors, entries.size());
    return ok;
}

}
