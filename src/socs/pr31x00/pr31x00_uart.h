#pragma once

#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/serial/serial_line.h"
#include "pr31x00_serial_wiring.h"
#include "pr31x00_uart_dma.h"
#include "pr31x00_uart_modem_lines.h"
#include "pr31x00_uart_transmitter.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

class Pr31x00Clock;
class SerialCradle;

class Pr31x00Uart : public Peripheral, public SerialLine {
public:
    explicit Pr31x00Uart(CerfEmulator& emu);
    ~Pr31x00Uart();

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    uint32_t MmioSize() const override { return 0x18u; }

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void PushRx(const uint8_t* data, size_t n) override;
    bool RxEmpty() const override;
    void SetRxDrainCallback(RxDrainFn cb) override;
    void SetModemInputs(bool cts, bool dsr, bool ri, bool dcd) override;
    LineConfig GetLineConfig() const override;
    void SetLineConfigCallback(LineConfigFn cb) override;
    void SetEndpoint(SerialEndpoint* ep) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

protected:
    virtual const char* TxSource() const = 0;

    /* Interrupt Status 2 UARTRXINT bit, verified from serial.dll sub_1861B3C
       (UART A RX-int mask 0x80000000, UART B 0x200000) + nk.exe OAL table
       0x9F433AD4 (Status2 bit 31 -> SYSINTR 12). */
    virtual uint32_t RxIntBit() const = 0;

    /* TMPR3911 §8.3.2 (page 8-8): Interrupt Status 2 UARTARXOVERRUNINT<30> / UARTBRXOVERRUNINT<20>. */
    virtual uint32_t RxOverrunIntBit() const = 0;

    /* Interrupt Status 2 UARTAEMPTYINT/UARTBEMPTYINT bit (TMPR3911 §8.3.2: THR
       and Transmit Shift Register both empty). serial.dll paces byte-by-byte
       PIO TX on it (sub_1862158 checks Status2 & this bit), so it must fire per
       drained TX byte or interrupt-driven TX stalls after byte 1. */
    virtual uint32_t TxEmptyIntBit() const = 0;

    /* Interrupt Status 2 UARTATXINT<26>/UARTBTXINT<16> (TMPR3911 §8.3.2);
       serial.dll PutBytes TX-pace mask [HWObj+0x94]=0x04000000/0x00010000
       (sub_1861B3C). */
    virtual uint32_t TxAvailIntBit() const = 0;

    /* Interrupt Status 2 UARTADMAFULLINT<23>/UARTBDMAFULLINT<13> and
       UARTADMAHALFINT<22>/UARTBDMAHALFINT<12> (TMPR3911 §8.3.2). */
    virtual uint32_t DmaFullIntBit() const = 0;
    virtual uint32_t DmaHalfIntBit() const = 0;

    /* ENUARTACLK<1> / ENUARTBCLK<0> in Clock Control (TMPR3911 §6.3.1 p6-6). */
    virtual uint32_t ClockEnableBit() const = 0;

private:
    void       WriteCtl1(uint32_t addr, uint32_t value);
    void       WriteTxHold(uint32_t addr, uint32_t value);
    void       TransmitDma(uint32_t addr, uint32_t control);
    void       DeliverTx(const uint8_t* data, size_t n);
    void       ApplyClockState();
    void       RaiseTxInts(const Pr31x00UartTransmitter::TxInts& ints);
    LineConfig ComputeLineConfigLocked() const;
    bool       UartClockRunning() const;
    void       OnRxLineIdle();
    void       RaiseRxInts(const Pr31x00UartDma::RxInts& ints);
    void       NotifyControlLines(bool dtr, bool rts);
    void       FireLineConfig();

    uint32_t    ctl1_ = 0;
    std::string tx_line_;

    std::optional<Pr31x00SerialModem> modem_;

    mutable std::mutex mu_;

    std::recursive_mutex endpoint_mu_;
    SerialEndpoint*      endpoint_ = nullptr;

    Pr31x00UartDma*         dma_         = nullptr;
    uint32_t                dma_port_    = 0;
    Pr31x00UartTransmitter* tx_          = nullptr;
    uint32_t                tx_port_     = 0;
    Pr31x00UartModemLines*  modem_lines_ = nullptr;
    uint32_t                modem_port_  = 0;

    uint32_t ctl2_baud_ = 0;
    uint32_t   fired_baud_ = 0;
    LineConfig line_cfg_;

    Pr31x00Clock* module_clock_ = nullptr;

    RxDrainFn    rx_drain_cb_;
    LineConfigFn line_cfg_cb_;

    std::unique_ptr<SerialCradle> cradle_;
};
