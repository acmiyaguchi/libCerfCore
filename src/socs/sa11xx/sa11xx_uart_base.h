#pragma once

#include "../../execution/guest_cycle_clock.h"
#include <cerfcore/peripherals/peripheral_base.h>
#include "sa11xx_uart_regs.h"

#include <cstdint>
#include <mutex>
#include <vector>

class Sa11xxDmaReceiveFifo;
class Sa11xxDmaTransmitFifo;
class Sa11xxExternalClocks;
class Sa11xxUartReceiver;
class Sa11xxUartTransmitter;
class UartEndpoint;
class UartLineDevice;

/* SA-1110 §11.9 / §11.11: SP1, SP2 and SP3 share one UART register surface (UTCR0..3, UTDR,
   UTSR0/1). */
class Sa11xxUartBase : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioSize() const override { return 0x00010000u; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

    void AttachEndpoint(UartEndpoint* endpoint);
    void AttachDevice(UartLineDevice* device);
    void PushRxBurst(const uint8_t* data, size_t n);
    void PushRxBurstNow(const uint8_t* data, size_t n);
    void OnTransmitted(const std::vector<uint8_t>& out);
    void OnReceived();
    bool BitClock(const sa11xx_uart::Control& ctl, GuestCycleClock::Rate& rate) const;

protected:
    virtual const char* ChannelName() const = 0;
    virtual int         IntcSourceBit() const { return -1; }
    virtual int         SampleClockPin() const { return -1; }
    virtual uint32_t    TransmitDeviceSelect() const = 0;
    virtual uint32_t    ReceiveDeviceSelect() const = 0;
    virtual Sa11xxDmaTransmitFifo& TransmitFifo() = 0;
    virtual Sa11xxDmaReceiveFifo&  ReceiveFifo() = 0;

private:
    mutable std::mutex state_mtx_;

    uint32_t utcr0_ = 0;
    uint32_t utcr1_ = 0;
    uint32_t utcr2_ = 0;
    uint32_t utcr3_ = 0;
    uint32_t utcr4_ = 0;
    UartEndpoint*        endpoint_ = nullptr;
    UartLineDevice*      device_   = nullptr;
    std::vector<uint8_t> tx_line_;
    bool intc_asserted_ = false;
    GuestCycleClock*       clock_   = nullptr;
    Sa11xxExternalClocks*  ext_     = nullptr;
    Sa11xxUartTransmitter* tx_      = nullptr;
    Sa11xxUartReceiver*    rx_      = nullptr;
    uint32_t               port_    = 0;
    uint32_t               rx_port_ = 0;

    void TxByte(uint8_t b);
    void FlushLine();
    void Emit(const std::vector<uint8_t>& out);
    uint32_t Utsr1Locked() const;
    uint32_t ComputeUtsr0Locked() const;
    uint64_t DmaMovedLocked() const;
    void     RefreshIrqLocked();
    void     OnResetLine();
    void     OnClockChange();

    uint32_t ReadReg(uint32_t off);
    void     WriteReg(uint32_t off, uint32_t value);

    static bool IsKnown(uint32_t off) {
        return off == 0x00 || off == 0x04 || off == 0x08 ||
               off == 0x0C || off == 0x10 || off == 0x14 ||
               off == 0x1C || off == 0x20;
    }
};
