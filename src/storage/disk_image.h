#pragma once

#include <cerfcore/platform/host_file.h>

#include <cstdint>
#include <string>

/* File-backed emulated disk, one value per attached drive (not a singleton).
   `size` is the logical capacity reported to the guest; the file grows on write
   (never pre-extended), so a blank image costs ~0 host disk. */
class DiskImage {
public:
    DiskImage() = default;
    DiskImage(const DiskImage&)            = delete;
    DiskImage& operator=(const DiskImage&) = delete;

    static constexpr uint32_t kSectorSize = 512u;

    /* Removable media can require an existing file instead of creating a blank. */
    bool Open(const std::string& path, uint64_t size_bytes, bool create_if_missing = true);

    bool     IsOpen()      const { return file_.IsOpen(); }
    uint64_t SectorCount() const { return sector_count_; }

    /* 512-byte LBA I/O; false on out-of-range or host error so the caller
       raises ATA ERR rather than fabricating data. */
    bool ReadSectors (uint64_t lba, uint32_t count, void*       dst);
    bool WriteSectors(uint64_t lba, uint32_t count, const void* src);

    /* Deallocate [lba, lba+count) so it reads back zero, keeping the file sparse
       (FSCTL_SET_ZERO_DATA). */
    bool PunchHole(uint64_t lba, uint32_t count);

private:
    host_platform::HostFile file_;
    uint64_t                sector_count_ = 0;
};
