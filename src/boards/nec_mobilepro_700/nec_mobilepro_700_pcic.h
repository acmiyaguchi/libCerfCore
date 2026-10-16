#pragma once

#include <cerfcore/core/service.h>
#include "../../peripherals/pcic82365/pcic82365.h"
#include "../../peripherals/pcmcia/pcmcia_slot.h"

#include <cstdint>
#include <mutex>

/* Dual-socket Intel 82365SL PCIC (NetBSD hpcmips dev/ic/i82365.c).
   ExCA file banked: index>>6 = socket, index&0x3F = register. */
class NecMobilePro700Pcic : public Service, public PcmciaSlotHost {
public:
    explicit NecMobilePro700Pcic(CerfEmulator& emu);

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override { slot0_.OnShutdown(); slot1_.OnShutdown(); }

    /* port 0 = index (0x140003E0), port 1 = data (0x140003E1). */
    uint8_t ReadPcicByte (uint32_t port);
    void    WritePcicByte(uint32_t port, uint8_t value);

    /* PC Card memory-space window: ISA-MEM byte offset -> the ExCA MEM_MAP
       decode of whichever socket owns an enabled window there -> attribute or
       common memory (Pcic82365::MapMem). */
    uint8_t  ReadCardMem8 (uint32_t off);
    uint16_t ReadCardMem16(uint32_t off);
    void     WriteCardMem8 (uint32_t off, uint8_t  value);
    void     WriteCardMem16(uint32_t off, uint16_t value);

    /* PC Card I/O-space window: ISA-IO byte offset -> the ExCA IO_MAP decode
       (Pcic82365::MapIo) -> card I/O. Excludes the index/data ports. */
    uint8_t  ReadCardIo8 (uint32_t off);
    uint16_t ReadCardIo16(uint32_t off);
    void     WriteCardIo8 (uint32_t off, uint8_t  value);
    void     WriteCardIo16(uint32_t off, uint16_t value);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);
    void PostRestore();

    /* PcmciaSlotHost. */
    void OnCardDetectChanged(PcmciaSlot& slot) override;
    void OnCardIrqAsserted  (PcmciaSlot& slot) override;
    void OnCardIrqDeasserted(PcmciaSlot& slot) override;

private:
    int         BankOf(const PcmciaSlot& slot) const { return &slot == &slot1_ ? 1 : 0; }
    PcmciaSlot& Slot(int bank) { return bank ? slot1_ : slot0_; }
    Pcic82365&  Exca(int bank) { return bank ? exca1_ : exca0_; }

    uint8_t ReadDataLocked();
    bool    WriteDataLocked(uint8_t value, int* bank, bool* power_on);
    void    UpdateGiuLine();

    /* Decode an ISA window offset through both sockets' ExCA windows. Runs
       under state_mutex_ and RELEASES it before the caller touches the slot,
       so the slot's bus_mutex_ (above the controller mutex in the lock
       hierarchy) is never taken while state_mutex_ is held. */
    bool DecodeMem(uint32_t off, int* bank, uint32_t* card_addr,
                   bool* attribute, bool* writable);
    bool DecodeIo (uint32_t off, int* bank, uint32_t* card_io);

    PcmciaSlot slot0_;
    PcmciaSlot slot1_;
    Pcic82365  exca0_;
    Pcic82365  exca1_;

    mutable std::mutex state_mutex_;
    uint8_t index_ = 0u;
    bool    card_irq_[2] = { false, false };

    uint8_t voltage_select_[2] = { 0u, 0u };
};
