#pragma once

#include <cerfcore/core/service.h>
#include "ce_imgfs_patcher.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

/* Recompose the cerf_guest stub PE into a replacement for a victim IMGFS module -
   the shared core of both IMGFS injectors (flat-container and NAND), which differ
   only in placement substrate and write sink. */
class ImgfsVictimRecomposer : public Service {
public:
    using Service::Service;

    struct Result {
        std::vector<uint8_t> new_hdr;                          /* e32_rom + o32 array */
        std::vector<cerf::ce_imgfs_patcher::PackedSlot> slots; /* relocated section data */
    };

    /* Returns nullopt (logged) when the victim is structurally unusable or the stub
       cannot pack into `num_sections` slots; CerfFatalExit on a CERF-side stub error
       (unopenable / unparseable PE, unhandled relocation). */
    std::optional<Result> Recompose(std::span<const uint8_t> orig_hdr,
                                    size_t                    num_sections,
                                    const std::string&        stub_path);
};
