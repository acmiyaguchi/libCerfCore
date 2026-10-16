#pragma once

#include "../core/service.h"
#include "../lcd/lcd_content_latch.h"
#include "frame_source.h"

#include <cstdint>

class FrameRenderer : public Service, public FrameSource {
public:
    using Service::Service;
    ~FrameRenderer() override = default;

    bool HasFrame() override = 0;

    virtual void RearmContentLatch() { latch_.Rearm(); }

    void RenderInto(uint32_t* dib_bgra32,
                    uint32_t  width,
                    uint32_t  height) override = 0;

    virtual void PresentedSize(uint32_t& w, uint32_t& h) = 0;

protected:
    LcdContentLatch latch_;
};
