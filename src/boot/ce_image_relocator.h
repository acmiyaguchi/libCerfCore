#pragma once

#include <cstdint>
#include <vector>

class PeImage;

namespace cerf::ce_image_relocator {

/* Pre-applied: injection sets e32_vbase==BasePtr so the kernel skips Relocate().
   Each fixup is rebased by section_realaddr[] of the section it targets - writable
   data on a slot-0 per-process base, code on slot-1 shared; a single global delta
   would leave writable data in the shared region. code_delta = out-of-section. */
void ApplyRelocations(std::vector<uint8_t>&         bytes,
                      const PeImage&                pe,
                      const std::vector<uint32_t>&  section_realaddr,
                      int32_t                       code_delta,
                      uint32_t&                     out_patched,
                      uint32_t&                     out_unhandled);

}
