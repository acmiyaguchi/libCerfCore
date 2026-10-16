#include "disk_image.h"

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

bool DiskImage::ReadSectors(uint64_t lba, uint32_t count, void* dst) {
    if (!file_.IsOpen() || lba + count > sector_count_) return false;
    const uint64_t off  = lba * kSectorSize;
    const uint32_t want = count * kSectorSize;
    auto* out = static_cast<uint8_t*>(dst);

    uint64_t psize = 0;
    if (!file_.Size(psize)) return false;
    if (off >= psize) { std::memset(out, 0, want); return true; }

    const uint64_t avail   = psize - off;
    const uint32_t in_file = (avail >= want) ? want : static_cast<uint32_t>(avail);
    if (!file_.SeekTo(off)) return false;
    uint32_t got = 0;
    if (!file_.Read(out, in_file, got) || got != in_file) return false;
    if (in_file < want) std::memset(out + in_file, 0, want - in_file);
    return true;
}

bool DiskImage::WriteSectors(uint64_t lba, uint32_t count, const void* src) {
    if (!file_.IsOpen() || lba + count > sector_count_) return false;
    if (!file_.SeekTo(lba * kSectorSize)) return false;
    const uint32_t want = count * kSectorSize;
    uint32_t put = 0;
    return file_.Write(src, want, put) && put == want;
}

bool DiskImage::PunchHole(uint64_t lba, uint32_t count) {
    if (!file_.IsOpen() || lba + count > sector_count_) return false;
    return file_.ZeroRange(lba * kSectorSize, (lba + count) * kSectorSize);
}

}
