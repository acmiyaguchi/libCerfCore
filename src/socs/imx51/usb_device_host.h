#pragma once

#include <cstdint>

class StateWriter;
class StateReader;

/* Host-side driver for the i.MX51 USB-OTG device controller (Imx51Usboh3):
   CERF emulates the ChipIdea device controller and a board service implements
   this seam to play the host PC. Calls arrive on the JIT thread. ep = USB
   endpoint number (control = 0). */
class UsbDeviceHost {
public:
    virtual ~UsbDeviceHost() = default;

    /* Hibernation: a host driver holding mutable enumeration/protocol state
       serializes it here; Imx51Usboh3 forwards from its own Save/Restore (same
       pattern as UartEndpoint <- Imx51Uart2). No-op default for a stateless host. */
    virtual void SaveState(StateWriter&) {}
    virtual void RestoreState(StateReader&) {}

    /* Device finished a host-driven bus reset (guest cleared USBSTS.URI). */
    virtual void OnDeviceReset() = 0;

    /* Device->host IN transfer completed: `len` bytes at `data` for `ep`. */
    virtual void OnDeviceIn(uint32_t ep, const uint8_t* data, uint32_t len) = 0;

    /* Device ready for a host->device OUT on `ep`; host writes up to `max`
       bytes into `dst`, returns the count (0 = zero-length packet). */
    virtual uint32_t OnDeviceOut(uint32_t ep, uint8_t* dst, uint32_t max) = 0;
};
