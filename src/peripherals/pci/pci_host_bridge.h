#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

class PciDevice;
class StateWriter;
class StateReader;

/* Abstract PCI host bridge. CtrlReadReg/CtrlWriteReg are the host-bridge control
   registers (PCIINIT00/PCIW0) delegated in from the SoC block that owns them;
   WindowRead/WindowWrite are the PCI memory-window cycles forwarded from the window
   Peripheral. */
class PciHostBridge : public Service {
public:
    using Service::Service;

    virtual void RegisterPciDevice(PciDevice* dev) = 0;

    virtual uint32_t CtrlReadReg(uint32_t block_off) = 0;
    virtual void     CtrlWriteReg(uint32_t block_off, uint32_t value) = 0;

    virtual uint32_t WindowRead(uint32_t addr, unsigned size) = 0;
    virtual void     WindowWrite(uint32_t addr, uint32_t value, unsigned size) = 0;

    /* PCI I/O-space cycles forwarded from the PCI I/O-window Peripheral. */
    virtual uint32_t WindowIoRead(uint32_t pci_io, unsigned size) = 0;
    virtual void     WindowIoWrite(uint32_t pci_io, uint32_t value, unsigned size) = 0;

    /* Hibernation: serialize the bridge control regs + each registered device's
       state. Driven by the enumerated PCI-window Peripheral's SaveState. */
    virtual void SaveState(StateWriter& w)    = 0;
    virtual void RestoreState(StateReader& r) = 0;
    virtual void PostRestore()                = 0;
};
