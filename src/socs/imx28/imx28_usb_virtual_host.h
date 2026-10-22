#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

class Imx28Usbctrl;
class StateReader;
class StateWriter;

/* USB 2.0 chapter 9 enumeration of the device on USBCTRL0. */
class Imx28UsbVirtualHost : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void Attach(Imx28Usbctrl& port);
    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);
    void PostRestore();

private:
    enum Stage : uint32_t { kIdle, kDebounce, kReset, kRecovery, kData, kStatus };

    void OnLink();
    void OnPrime();
    void Step();
    void StartRequest();
    void StepData();
    void StepStatus();
    void FinishRequest();
    void Arm(int64_t ns);
    void Rearm();
    [[noreturn]] void Die(const char* what, uint32_t a, uint32_t b) const;

    Imx28Usbctrl*            port_  = nullptr;
    GuestCycleClock*         clock_ = nullptr;
    GuestCycleClock::Event*  event_ = nullptr;
    uint32_t                 stage_   = kIdle;
    uint32_t                 request_ = 0;
    uint32_t                 length_  = 0;
    uint32_t                 armed_   = 0;
    uint32_t                 linked_  = 0;
    int64_t                  due_ns_  = 0;
    uint8_t                  setup_[8] = {};
    std::vector<uint8_t>     data_;
    std::vector<uint8_t>     config_;
};

}
