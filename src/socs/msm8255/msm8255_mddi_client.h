#pragma once

#include <cerfcore/core/service.h>
#include "../../lcd/panel_scanout.h"

#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

struct Msm8255MddiClientCapability {
    uint16_t bitmap_width;
    uint16_t bitmap_height;
    uint16_t display_window_width;
    uint16_t display_window_height;
    uint16_t mfr_name;
    uint16_t product_code;
};

/* Linux arch/arm/mach-msm video-msm mddihosti.h:
   mddi_video_stream_packet_type. */
struct Msm8255MddiVideoStream {
    uint16_t format_descriptor;
    uint16_t pixel_attributes;
    uint16_t x_left_edge;
    uint16_t y_top_edge;
    uint16_t x_right_edge;
    uint16_t y_bottom_edge;
    uint16_t x_start;
    uint16_t y_start;
    uint16_t pixel_count;
};

struct Msm8255MddiSurface {
    const uint8_t*   pixels;
    uint32_t         stride_bytes;
    uint32_t         width;
    uint32_t         height;
    PanelPixelFormat format;
    bool             visible;
};

class Msm8255MddiClient : public Service {
public:
    using Service::Service;

    virtual Msm8255MddiClientCapability Capability() const = 0;

    virtual uint32_t ReadRegister(uint32_t address) = 0;
    virtual void     WriteRegister(uint32_t address, uint32_t value) = 0;

    virtual void WriteVideoStream(const Msm8255MddiVideoStream& video,
                                  uint32_t data_pa, uint32_t data_bytes) = 0;

    virtual Msm8255MddiSurface Surface() const = 0;

    virtual void SaveState(StateWriter& w) = 0;
    virtual void RestoreState(StateReader& r) = 0;
};

}
