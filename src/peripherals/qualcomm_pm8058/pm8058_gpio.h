#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

class StateWriter;
class StateReader;

class Pm8058Gpio : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    /* Linux include/linux/mfd/pm8058.h PM8058_NUM_GPIO_IRQS 40, and
       drivers/mfd/pm8058-core.c REG_GPIO_CTRL(x) (0x0150 + (x)). */
    static constexpr uint32_t kGpios   = 40u;
    static constexpr uint16_t kRegBase = 0x0150u;

    /* Linux drivers/mfd/pm8058-core.c pm8058_gpio_mux_cfg writes bank[6]. */
    static constexpr uint32_t kBanks = 6u;

    static bool Owns(uint16_t reg) {
        return reg >= kRegBase && reg < kRegBase + kGpios;
    }

    uint8_t ReadReg(uint16_t reg);
    void    WriteReg(uint16_t reg, uint8_t value);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    void Reset();

    uint8_t bank_data_[kGpios][kBanks] = {};
    uint8_t read_bank_[kGpios]         = {};
};
