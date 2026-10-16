#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

/* Decides the runtime VA base for an injected guest-additions module: the
   victim's own vbase when the image fits its fixed slot, or a relocated
   section-1 vbase the kernel will reserve when the image overflows it. */
class GuestModulePlacer : public Service {
public:
    using Service::Service;

    uint32_t ComputeVbase(uint32_t orig_vbase, uint32_t orig_slot_base,
                          uint32_t image_size, uint32_t ce_major,
                          uint32_t e32_off_vbase, const char* victim_name);

    /* The CE5/WM5 loader per-processes only WRITE & !SHARED sections; flagging
       the injected pid-keyed stub's writable section SHARED keeps it one shared
       copy so the second loader (the device.exe carrier) doesn't fault placing a
       per-process copy at a slot base. Used by both the XIP and IMGFS injectors. */
    uint32_t EffSectionFlags(uint32_t flags) const;
};
