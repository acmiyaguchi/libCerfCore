#pragma once

#include "../core/service.h"
#include "pointer_source.h"

#include <cstdint>

namespace cerfcore {

/* Absolute mouse (hover, buttons, wheel) for guest additions. sx,sy are
   guest-surface pixels (same space as TouchInput); the concrete normalizes. */
/* button_mask bits, shared by HostCanvas and the channel adapter. */
constexpr uint32_t kPointerLeft   = 0x1u;
constexpr uint32_t kPointerRight  = 0x2u;
constexpr uint32_t kPointerMiddle = 0x4u;

class PointerInput : public Service, public PointerSource {
public:
    using Service::Service;
    ~PointerInput() override = default;

    virtual void OnMove (int sx, int sy, uint32_t button_mask) = 0;
    virtual void OnWheel(int sx, int sy, int delta)            = 0;
    virtual void OnCaptureLost()                               = 0;

    /* PointerSource - absolute GA mouse; outranks stock devices at boot. */
    std::string    SourceName()       const override { return "Guest Additions mouse"; }
    const char*    IconResourceName() const override { return "ICON_INPUT_GA_POINTER"; }
    int            SourcePriority()   const override { return 100; }
    PointerKind    Kind()             const override { return PointerKind::Absolute; }
};

}
