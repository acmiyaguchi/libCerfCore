#pragma once

#include "../../peripherals/serial/serial_16550.h"

#include <cstdint>
#include <memory>

class StateWriter;
class StateReader;

/* EM-500 companion serial/modem port: PC16550D modem UART (serial.dll ser16550
   PDD; companion 0x8680 stride 4, base GetVMem 0xAA008000 via serial.dll mapper
   0xF56CEC) + its modem-block/socket control latches + INTR line. */
class CasioJx670Modem {
public:
    void Init();

    bool TryReadByte (uint32_t off, uint8_t&  out);
    bool TryWriteByte(uint32_t off, uint8_t   value);
    bool TryReadHalf (uint32_t off, uint16_t& out);
    bool TryWriteHalf(uint32_t off, uint16_t  value);
    bool TryReadWord (uint32_t off, uint32_t& out);
    bool TryWriteWord(uint32_t off, uint32_t  value);

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);
    void PostRestore();

private:
    void OnUartIrq(bool asserted);

    static constexpr uint32_t k16550Base = 0x8680u;   /* regs 0..7 stride 4 (serial.dll dword_F58130 [1..8]) */
    bool In16550(uint32_t off) const {
        return off >= k16550Base && off < k16550Base + 8u * 4u &&
               ((off - k16550Base) & 3u) == 0u;
    }

    std::unique_ptr<Serial16550> uart_;
    uint8_t  ctrl_8600_   = 0;   /* serial.dll dword_F58130[0]=0x600 -> companion 0x8600 */
    uint16_t socket_a1c0_ = 0;
    uint16_t socket_a1c4_ = 0;
};
