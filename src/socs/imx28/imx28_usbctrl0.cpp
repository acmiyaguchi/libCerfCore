#include <cerfcore/core/cerf_emulator.h>
#include "imx28_usb_virtual_host.h"
#include "imx28_usbctrl.h"

namespace cerfcore {

namespace {

class Imx28Usbctrl0 : public Imx28Usbctrl {
public:
    explicit Imx28Usbctrl0(CerfEmulator& emu) : Imx28Usbctrl(emu, 0u, 0x80080000u) {}

    void OnReady() override {
        Imx28Usbctrl::OnReady();
        host_ = &emu_.Get<Imx28UsbVirtualHost>();
        host_->Attach(*this);
    }

    void SaveState(StateWriter& w) override {
        Imx28Usbctrl::SaveState(w);
        host_->SaveState(w);
    }

    void RestoreState(StateReader& r) override {
        Imx28Usbctrl::RestoreState(r);
        host_->RestoreState(r);
    }

    void PostRestore() override {
        Imx28Usbctrl::PostRestore();
        host_->PostRestore();
    }

private:
    Imx28UsbVirtualHost* host_ = nullptr;
};

}  /* namespace */

REGISTER_SERVICE(Imx28Usbctrl0);

}
