#pragma once

#include <cstdint>
#include <mutex>

class StateWriter;
class StateReader;

inline constexpr uint32_t kSlotCsrA          = 0x00u;
inline constexpr uint32_t kSlotCsrB          = 0x04u;
inline constexpr uint32_t kSerialBlockSize   = 0x08u;

inline constexpr uint16_t kCsrAW1cMask       = 0xF618u;  /* SERA_INTR_MASK */
inline constexpr uint16_t kCsrAReadOnlyMask  = 0x00E6u;  /* RI|DSR|TX_FULL|CTS|CD */
inline constexpr uint16_t kCsrARwMask        = 0x0901u;  /* SERA_SERIAL_ON + bits 8,11 */

inline constexpr uint16_t kSeraTxIntr        = 0x0010u;
inline constexpr uint16_t kSerbTxEn          = 0x2000u;

class P2FpgaSerial {
public:
    uint16_t Read(uint32_t slot_off);

    /* Returns true on SERB_TX_EN 0→1 in CSR B - DEBUG_SER uses
       this rising edge to dispatch TX. */
    bool Write(uint32_t slot_off, uint16_t value);

    void SetCsrABits(uint16_t bits);

    /* Exact register snapshot: bypasses Write()'s W1C / read-only masking,
       which would otherwise corrupt the restored csr_a_. */
    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

    static bool IsValidOffset(uint32_t slot_off) {
        return slot_off == kSlotCsrA || slot_off == kSlotCsrB;
    }

private:
    mutable std::mutex state_mutex_;
    uint16_t           csr_a_ = 0;
    uint16_t           csr_b_ = 0;
};
