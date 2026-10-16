#include "omap3530_i2c.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/ti_tps65950/twl4030.h"
#include "omap3530_sdma.h"

namespace cerfcore {

namespace {

/* I2C1 - talks to the TWL4030 PMIC. Dispatch hooks routed to the
   TWL4030 slave service. */
class Omap3530I2c1 : public Omap3530I2cBank {
public:
    using Omap3530I2cBank::Omap3530I2cBank;
    uint32_t MmioBase() const override { return 0x48070000u; }

    void OnReady() override {
        twl_ = emu_.TryGet<Twl4030>();
        Omap3530I2cBank::OnReady();
    }
    void SaveState(StateWriter& w) override {
        Omap3530I2cBank::SaveState(w);
        if (twl_ != nullptr) twl_->SaveState(w);
    }
    void RestoreState(StateReader& r) override {
        Omap3530I2cBank::RestoreState(r);
        if (twl_ != nullptr) twl_->RestoreState(r);
    }
    void PostRestore() override {
        Omap3530I2cBank::PostRestore();
        if (twl_ != nullptr) twl_->PostRestore();
    }

protected:
    int TxSyncSource() const override { return Omap3530Sdma::kSyncI2c1Tx; }
    int RxSyncSource() const override { return Omap3530Sdma::kSyncI2c1Rx; }
    void DispatchWriteLocked(uint32_t guest_addr_for_diag,
                             uint8_t  slave_addr) override;
    void DispatchReadLocked (uint32_t guest_addr_for_diag,
                             uint8_t  slave_addr,
                             uint16_t count) override;

private:
    Twl4030* twl_ = nullptr;
};

void Omap3530I2c1::DispatchWriteLocked(uint32_t guest_addr_for_diag,
                                       uint8_t  slave_addr) {
    if (twl_ == nullptr || !twl_->MatchesAddress(slave_addr)) {
        Omap3530I2cBank::DispatchWriteLocked(guest_addr_for_diag,
                                             slave_addr);
        return;
    }
    twl_->TxnStart(slave_addr);
    while (!tx_fifo_.empty()) {
        twl_->TxnWriteByte(slave_addr, tx_fifo_.front());
        tx_fifo_.pop_front();
    }
}

void Omap3530I2c1::DispatchReadLocked(uint32_t guest_addr_for_diag,
                                      uint8_t  slave_addr,
                                      uint16_t count) {
    if (twl_ == nullptr || !twl_->MatchesAddress(slave_addr)) {
        Omap3530I2cBank::DispatchReadLocked(guest_addr_for_diag,
                                            slave_addr, count);
        return;
    }
    twl_->TxnStart(slave_addr);
    for (uint16_t i = 0; i < count; ++i) {
        rx_fifo_.push_back(twl_->TxnReadByte(slave_addr));
    }
}

class Omap3530I2c2 : public Omap3530I2cBank {
public:
    using Omap3530I2cBank::Omap3530I2cBank;
    uint32_t MmioBase() const override { return 0x48072000u; }
protected:
    int TxSyncSource() const override { return Omap3530Sdma::kSyncI2c2Tx; }
    int RxSyncSource() const override { return Omap3530Sdma::kSyncI2c2Rx; }
};
class Omap3530I2c3 : public Omap3530I2cBank {
public:
    using Omap3530I2cBank::Omap3530I2cBank;
    uint32_t MmioBase() const override { return 0x48060000u; }
    /* I2C3 is high-speed I2C with no SDMA wiring on OMAP3530 - uses
       its own internal FIFO and CPU-driven transfers. */
};

}  /* namespace */

REGISTER_SERVICE(Omap3530I2c1);
REGISTER_SERVICE(Omap3530I2c2);
REGISTER_SERVICE(Omap3530I2c3);

}
