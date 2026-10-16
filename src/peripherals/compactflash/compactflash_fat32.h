#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <string>
#include <vector>

namespace cerfcore {

/* Builds a FAT32 disk image from a set of host files, placed in the root
   directory. The image is real FAT32 (validated by CE's own FAT driver
   mounting it), sized to the larger of the requested capacity, the file
   payload, and the FAT32 minimum (>=65525 clusters). Used by the
   CompactFlash "Generate" menu. */
class CompactFlashFat32Builder : public Service {
public:
    using Service::Service;

    /* Writes a FAT32 image at out_path containing each file in files at
       the volume root (long names preserved). data_mb requests the usable
       data-region capacity in MiB (the card is never smaller than the
       files it holds, nor smaller than the FAT32 minimum). Returns false
       (and logs) on any host I/O error. */
    bool Build(const std::string& out_path,
               const std::vector<std::string>& files,
               uint32_t data_mb);
};

}
