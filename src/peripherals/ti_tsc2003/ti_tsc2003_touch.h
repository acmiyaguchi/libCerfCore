#pragma once

#include <cerfcore/core/service.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>

namespace cerfcore {

/* TI TSC2003 I2C touch-screen controller (datasheet SBAS162G). Ford SYNC2 APIM:
   I2C slave 0x49 (Fig.10 addr = 10010.A1.A0, strapped A1=0/A0=1) on the GPIO4
   software-I2C "HIC1:" bus, driven by the ROM's touch_tsc2003.dll (sub_C1471EA0). */
class Tsc2003Touch : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    void SetState(uint16_t adc_x, uint16_t adc_y, bool pen_down);

    /* Fires when the PENIRQ output (pen_down && PD0-enabled) changes - on the host
       pen edge OR a command's PD0 bit - so the board drives its pen GPIO; without
       it a read burst's PENIRQ-disable never reaches the pen line. */
    void SetPenIrqObserver(std::function<void(bool asserted)> cb);

    /* Drive PENIRQ only while pen-down AND the last command left PENIRQ enabled
       (PD0=0, datasheet Table II); the 0xC4/0xD4 burst sets PD=01 to suppress it
       mid-read, so gating on pen_down alone re-enters the driver's ISR mid-read. */
    bool PenIrqAsserted() const;

    /* HIC1 decoder surface (slave 0x49): each measurement is two transactions
       (touch_tsc2003.dll sub_C1471714) - a 1-byte command write, a 2-byte read. */
    void    WriteCommand(uint8_t cmd);   /* command byte, datasheet Fig.11 */
    uint8_t ReadResultByte();            /* next of 2 result bytes */

    uint16_t AdcX()    const { return adc_x_.load(std::memory_order_acquire); }
    uint16_t AdcY()    const { return adc_y_.load(std::memory_order_acquire); }
    bool     PenDown() const { return pen_down_.load(std::memory_order_acquire); }

private:
    void UpdatePenIrq();

    std::atomic<uint16_t> adc_x_     {0};
    std::atomic<uint16_t> adc_y_     {0};
    std::atomic<bool>     pen_down_  {false};
    std::atomic<bool>     penirq_en_ {true};
    uint16_t result_       = 0;
    uint8_t  read_idx_     = 0;
    bool     result_valid_ = false;
    std::function<void(bool)> penirq_obs_;
    std::mutex penirq_mu_;
    bool       last_penirq_ = false;
};

}
