#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace siemens_simatic_mp377 {

/* siemens_mp377_v1040 bspio.dll HWI_PNIO_GetInfo: family 4 variants
   1/2/4/8; nk.exe sub_80446E14: family 4, variant 2 = 800x600x16. */
enum class SiemensSimaticMp377PanelProfile {
    Inch12_800x600,
    Inch15_1024x768,
    Inch19_1280x1024,
};

struct SiemensSimaticMp377PanelDescriptor {
    SiemensSimaticMp377PanelProfile profile;
    uint8_t op_type_family;
    uint8_t op_type_variant;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
};

inline constexpr SiemensSimaticMp377PanelDescriptor Mp377PanelDescriptor(SiemensSimaticMp377PanelProfile p) {
    return p == SiemensSimaticMp377PanelProfile::Inch12_800x600    ? SiemensSimaticMp377PanelDescriptor{p, 4u, 2u, 800u, 600u, 16u}
           : p == SiemensSimaticMp377PanelProfile::Inch15_1024x768 ? SiemensSimaticMp377PanelDescriptor{p, 4u, 4u, 1024u, 768u, 16u}
                                                            : SiemensSimaticMp377PanelDescriptor{p, 4u, 8u, 1280u, 1024u, 16u};
}

class SiemensSimaticMp377Panel : public Service {
public:
    using Service::Service;

    virtual SiemensSimaticMp377PanelProfile Profile() const = 0;

    SiemensSimaticMp377PanelDescriptor Descriptor() const {
        return Mp377PanelDescriptor(Profile());
    }
};

inline constexpr uint32_t Mp377PanelWidth(SiemensSimaticMp377PanelProfile p) {
    return Mp377PanelDescriptor(p).width;
}

inline constexpr uint32_t Mp377PanelHeight(SiemensSimaticMp377PanelProfile p) {
    return Mp377PanelDescriptor(p).height;
}

} // namespace siemens_mp377
