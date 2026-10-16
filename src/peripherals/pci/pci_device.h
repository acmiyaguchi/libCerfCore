#pragma once

#include <cstdint>

class StateWriter;
class StateReader;

/* A device on a PCI bus, routed to by a PciHostBridge. The device owns its 256-byte
   type-0 config space (including BARs) and the memory its BARs map into PCI memory
   space. Config access is dword-granular (the VRC5477 OAL issues dword window cycles;
   the bridge does read-modify-write for sub-dword writes). */
class PciDevice {
public:
    virtual ~PciDevice() = default;

    /* Hibernation: a device with mutable config/BAR/register state overrides
       these; a stateless device keeps the no-op default. The bridge drives them
       (a PciDevice is not a Peripheral, so the hibernation walk can't reach it). */
    virtual void SaveState(StateWriter&) {}
    virtual void RestoreState(StateReader&) {}
    virtual void PostRestore() {}

    /* Location on bus 0 (the only bus the VRC5477 external host bridge drives). */
    virtual uint8_t PciDev() const = 0;
    virtual uint8_t PciFnc() const = 0;

    virtual uint32_t ConfigRead(uint32_t reg) = 0;             /* reg = dword-aligned byte offset 0..0xFC */
    virtual void     ConfigWrite(uint32_t reg, uint32_t value) = 0;

    /* BAR-mapped memory in PCI memory space. The bridge asks each device whether it
       claims a PCI address (a programmed BAR covers it) and routes the access. */
    virtual bool     MemClaims(uint32_t pci_addr) const = 0;
    virtual uint32_t MemRead(uint32_t pci_addr, unsigned size) = 0;
    virtual void     MemWrite(uint32_t pci_addr, uint32_t value, unsigned size) = 0;

    /* PCI I/O space (a bridge that forwards an I/O window - e.g. a CardBus bridge
       to a 16-bit card - claims a PCI I/O address). Default: no I/O. */
    virtual bool     IoClaims(uint32_t /*pci_io*/) const { return false; }
    virtual uint32_t IoRead(uint32_t /*pci_io*/, unsigned /*size*/) { return 0xFFFFFFFFu; }
    virtual void     IoWrite(uint32_t /*pci_io*/, uint32_t /*value*/, unsigned /*size*/) {}
};
