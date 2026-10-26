#include "disk_image.h"

#include <bit>
#include <cstring>

#include <cerfcore/core/log.h>
#include "../platform/host_error.h"

namespace cerfcore {

bool DiskImage::Open(const std::string& path, uint64_t size_bytes, bool create_if_missing) {
    const auto mode = create_if_missing ? host_platform::FileMode::kReadWriteAlways
                                        : host_platform::FileMode::kReadWriteExisting;
    if (!file_.Open(path, mode)) {
        LOG(Caution, "DiskImage: opening '%s' failed gle=%u\n",
            path.c_str(), host_platform::LastErrorCode());
        return false;
    }

    file_.MarkSparse();

    uint64_t physical = 0;
    if (!file_.Size(physical)) physical = 0;
    const uint64_t advertised = physical > size_bytes ? physical : size_bytes;
    sector_count_ = advertised / kSectorSize;
    LOG(Boot, "DiskImage: '%s' open, %llu sectors (%llu MB; logical %llu MB, "
              "physical %llu MB)\n",
        path.c_str(), static_cast<unsigned long long>(sector_count_),
        static_cast<unsigned long long>(advertised / (1024u * 1024u)),
        static_cast<unsigned long long>(size_bytes / (1024u * 1024u)),
        static_cast<unsigned long long>(physical / (1024u * 1024u)));
    return true;
}

bool DiskImage::OpenOverlay(const std::string& base_path, const std::string& overlay_path,
                            uint64_t size_bytes) {
    const std::string map_path = overlay_path + ".map";
    if (!base_.Open(base_path, host_platform::FileMode::kReadExisting) ||
        !file_.Open(overlay_path, host_platform::FileMode::kReadWriteAlways) ||
        !map_file_.Open(map_path, host_platform::FileMode::kReadWriteAlways)) {
        LOG(Caution, "DiskImage: opening base '%s' with overlay '%s' failed gle=%u\n",
            base_path.c_str(), overlay_path.c_str(), host_platform::LastErrorCode());
        return false;
    }
    file_.MarkSparse();

    uint64_t base_bytes = 0;
    uint64_t map_bytes  = 0;
    if (!base_.Size(base_bytes) || !map_file_.Size(map_bytes)) return false;
    const uint64_t advertised = base_bytes > size_bytes ? base_bytes : size_bytes;
    sector_count_ = advertised / kSectorSize;
    overlay_map_.assign(static_cast<size_t>((sector_count_ + 7u) / 8u), 0u);
    if (map_bytes > overlay_map_.size()) {
        LOG(Caution, "DiskImage: overlay map '%s' holds %llu bytes, %zu expected\n",
            map_path.c_str(), static_cast<unsigned long long>(map_bytes), overlay_map_.size());
        return false;
    }
    uint32_t got = 0;
    if (map_bytes != 0 &&
        (!map_file_.SeekTo(0) ||
         !map_file_.Read(overlay_map_.data(), static_cast<uint32_t>(map_bytes), got) ||
         got != map_bytes)) {
        return false;
    }
    overlay_ = true;

    uint64_t marked = 0;
    for (uint8_t b : overlay_map_) marked += static_cast<uint64_t>(std::popcount(b));
    LOG(Boot, "DiskImage: base '%s' read-only, overlay '%s', %llu sectors, %llu in overlay\n",
        base_path.c_str(), overlay_path.c_str(), static_cast<unsigned long long>(sector_count_),
        static_cast<unsigned long long>(marked));
    return true;
}

bool DiskImage::ReadRange(host_platform::HostFile& file, uint64_t lba, uint32_t count,
                          uint8_t* out) {
    const uint64_t off  = lba * kSectorSize;
    const uint32_t want = count * kSectorSize;

    uint64_t psize = 0;
    if (!file.Size(psize)) return false;
    if (off >= psize) { std::memset(out, 0, want); return true; }

    const uint64_t avail   = psize - off;
    const uint32_t in_file = (avail >= want) ? want : static_cast<uint32_t>(avail);
    if (!file.SeekTo(off)) return false;
    uint32_t got = 0;
    if (!file.Read(out, in_file, got) || got != in_file) return false;
    if (in_file < want) std::memset(out + in_file, 0, want - in_file);
    return true;
}

bool DiskImage::ReadSectors(uint64_t lba, uint32_t count, void* dst) {
    if (!file_.IsOpen() || lba + count > sector_count_) return false;
    auto* out = static_cast<uint8_t*>(dst);
    if (!overlay_) return ReadRange(file_, lba, count, out);

    uint32_t done = 0;
    while (done < count) {
        const bool from_overlay = InOverlay(lba + done);
        uint32_t   run          = 1;
        while (done + run < count && InOverlay(lba + done + run) == from_overlay) ++run;
        if (!ReadRange(from_overlay ? file_ : base_, lba + done, run, out + done * kSectorSize))
            return false;
        done += run;
    }
    return true;
}

bool DiskImage::MarkOverlay(uint64_t lba, uint32_t count) {
    for (uint64_t s = lba; s < lba + count; ++s)
        overlay_map_[s / 8u] |= static_cast<uint8_t>(1u << (s % 8u));
    const uint64_t first = lba / 8u;
    const uint64_t last  = (lba + count - 1u) / 8u;
    const uint32_t bytes = static_cast<uint32_t>(last - first + 1u);
    uint32_t put = 0;
    return map_file_.SeekTo(first) &&
           map_file_.Write(overlay_map_.data() + first, bytes, put) && put == bytes;
}

bool DiskImage::WriteSectors(uint64_t lba, uint32_t count, const void* src) {
    if (!file_.IsOpen() || lba + count > sector_count_) return false;
    if (!file_.SeekTo(lba * kSectorSize)) return false;
    const uint32_t want = count * kSectorSize;
    uint32_t put = 0;
    if (!file_.Write(src, want, put) || put != want) return false;
    return !overlay_ || count == 0 || MarkOverlay(lba, count);
}

bool DiskImage::PunchHole(uint64_t lba, uint32_t count) {
    if (!file_.IsOpen() || lba + count > sector_count_) return false;
    if (!file_.ZeroRange(lba * kSectorSize, (lba + count) * kSectorSize)) return false;
    return !overlay_ || count == 0 || MarkOverlay(lba, count);
}

}
