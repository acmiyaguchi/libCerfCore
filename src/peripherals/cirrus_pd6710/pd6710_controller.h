#pragma once

#include <cerfcore/core/service.h>
#include "../pcic82365/pcic82365.h"
#include "../pcmcia/pcmcia_slot.h"

#include <cstdint>
#include <mutex>

namespace cerfcore {

/* Cirrus PD6710 single-socket PCMCIA host adapter. The PCIC index/data
   port pair and the window mapping registers live here; the I/O and
   memory window peripherals decode guest accesses through MapIo /
   MapMem and reach the card via Slot(). */
class Pd6710Controller : public Service, public PcmciaSlotHost {
public:
    explicit Pd6710Controller(CerfEmulator& emu);

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override { slot_.OnShutdown(); }

    PcmciaSlot& Slot() { return slot_; }

    /* PCIC register file + the resident card (forwarded from the I/O window
       peripheral, which is on the hibernation walk; the controller is not). */
    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);
    void PostRestore();

    /* PCIC register file at I/O ports 0x3E0 (INDEX) / 0x3E1 (DATA). */
    uint8_t ReadPcicByte (uint32_t port);
    void    WritePcicByte(uint32_t port, uint8_t value);

    /* I/O window decode: bus_io is the 16-bit I/O bus address; on hit
       *card_io receives the card-local I/O address. */
    bool MapIo(uint32_t bus_io, uint32_t* card_io);

    /* Memory window decode: bus_off is the offset into the 16 MB
       PCMCIA memory space; on hit *card_addr receives the card-local
       address, *attribute the REG bit, *writable the inverted
       write-protect bit. */
    bool MapMem(uint32_t bus_off, uint32_t* card_addr,
                bool* attribute, bool* writable);

    /* PcmciaSlotHost. */
    void OnCardDetectChanged(PcmciaSlot& slot) override;
    void OnCardIrqAsserted  (PcmciaSlot& slot) override;
    void OnCardIrqDeasserted(PcmciaSlot& slot) override;

private:
    uint8_t ReadIndexedDataLocked();
    /* Returns true when the write flipped socket power; *power_on is
       the new state. Applied to the slot after the lock drops -
       SetPowered takes the slot bus lock, which ranks above
       state_mutex_. */
    bool WriteIndexedDataLocked(uint8_t value, bool* power_on);


    PcmciaSlot slot_;

    mutable std::mutex state_mutex_;

    uint8_t index_ = 0u;

    uint8_t reg_chip_info_ = 0u;   /* PD6710-specific chip-detect probe register (0x1F) */

    Pcic82365 exca_;
};

}
