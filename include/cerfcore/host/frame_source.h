#pragma once

#include <cstdint>

/* A producer of guest-surface frames for CanvasPresenter - the minimal
   surface the presentation core needs to draw one window's content.

   Deliberately NOT a Service: the single board FrameRenderer implements it,
   AND each PCMCIA VGA card owns its own per-instance producer for its own
   window, so the presenter must depend on a multi-instance-safe interface
   rather than on the single-winner FrameRenderer service.

   RenderInto MUST fill every byte of dib_bgra32[0..width*height-1]: the
   presenter does not pre-clear it and it still holds the previous frame.
   width/height are the native guest-surface dimensions, not host-window
   pixels. */
class FrameSource {
public:
    virtual ~FrameSource() = default;

    virtual bool HasFrame() = 0;
    virtual void RenderInto(uint32_t* dib_bgra32,
                            uint32_t  width,
                            uint32_t  height) = 0;
};
