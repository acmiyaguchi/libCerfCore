#pragma once

#include "../../peripherals/usb/usb_hub.h"

namespace cerfcore {

class FordSyncGen2MediaHub final : public UsbHub {
public:
    static constexpr int kSdPort = 0;
    static constexpr int kUsbPort = 1;
    static constexpr int kPortCount = 2;
    static constexpr uint32_t kStateKind = 4;

    FordSyncGen2MediaHub();
    uint32_t StateKind() const override { return kStateKind; }
    void SetMedia(int port, std::unique_ptr<UsbDevice> device);
};

}
