#pragma once

#include <cstdint>

class StateWriter;
class StateReader;

class Pcic82365 {
public:
    /* INTERFACE_STATUS (0x01) reports card-detect from this; the owner sets it
       from slot occupancy. */
    void SetCardPresent(bool present) { card_present_ = present; }

    /* i82365reg.h: PCIC_IF_STATUS_READY (0x01 bit5) "really READY/!BUSY";
       for PCIC_INTR_CARDTYPE_IO it reads BUSY while the card IREQ# is asserted. */
    void SetCardIrq(bool asserted) { card_irq_ = asserted; }

    /* True iff REG_POWER_CONTROL holds the powered-on pattern (VCC | output enable). */
    bool CardPoweredByReg() const;

    /* Latch a REG_CARD_STATUS_CHANGE bit (read-clears on register 0x04). */
    void LatchStatusChange(uint8_t bits) { reg_card_status_change_ |= bits; }

    /* True iff REG_INTERRUPT_AND_GENERAL_CONTROL routes the card IRQ (low nibble set). */
    bool CardIrqRouted() const { return (reg_interrupt_and_gen_ctrl_ & 0x0Fu) != 0u; }
    /* True iff REG_STATUS_CHANGE_INT_CONFIG enables the card-detect status-change IRQ. */
    bool DetectChangeIntEnabled() const { return (reg_status_change_int_cfg_ & 0x08u) != 0u; }
    /* Management (status-change) interrupt is asserted while a detect-change is latched
       and enabled; clears when the guest read-clears reg 0x04 (CSC_DETECT_CHANGE=0x08). */
    bool DetectChangeInterruptActive() const {
        return (reg_card_status_change_ & 0x08u) != 0u && DetectChangeIntEnabled();
    }

    /* True iff `index` is a standard register this file owns; the owner handles
       the rest (chip identity/control). */
    static bool Owns(uint8_t index);

    uint8_t ReadReg(uint8_t index);
    /* Returns true iff the write changed socket power; *power_on = the new state. */
    bool WriteReg(uint8_t index, uint8_t value, bool* power_on);

    /* I/O window decode: bus_io is the 16-bit I/O bus address; on hit *card_io is
       the card-local I/O address. */
    bool MapIo(uint32_t bus_io, uint32_t* card_io) const;
    /* Memory window decode: bus_off is the offset into PC Card memory space; on hit
       *card_addr is the card-local address, *attribute the REG bit, *writable the
       inverted write-protect bit. */
    bool MapMem(uint32_t bus_off, uint32_t* card_addr,
                bool* attribute, bool* writable) const;

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    bool    card_present_               = false;
    bool    card_irq_                   = false;
    uint8_t reg_power_control_          = 0u;
    uint8_t reg_interrupt_and_gen_ctrl_ = 0u;
    uint8_t reg_card_status_change_     = 0u;
    uint8_t reg_status_change_int_cfg_  = 0u;
    uint8_t reg_window_enable_          = 0u;
    uint8_t reg_io_window_control_      = 0u;

    uint8_t io_start_lo_[2] = {};
    uint8_t io_start_hi_[2] = {};
    uint8_t io_end_lo_  [2] = {};
    uint8_t io_end_hi_  [2] = {};
    uint8_t io_off_lo_  [2] = {};
    uint8_t io_off_hi_  [2] = {};
    uint8_t mem_reg_[5][6]  = {};   /* [win][start_lo,start_hi,end_lo,end_hi,off_lo,off_hi] */
    uint8_t mem_page_[5]    = {};   /* MEM_MAP0..4 window-page (high address bits) */
    uint8_t timing_[6]      = {};
};
