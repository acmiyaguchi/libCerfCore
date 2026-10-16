#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"
#include "pxa2xx_ac97_link.h"

#include <cstdint>
#include <functional>

class IrqController;

class Pxa2xxAc97Interrupt : public Service, public Pxa2xxAc97LinkListener {
public:
    using Service::Service;

    static constexpr uint64_t kNever = Pxa2xxAc97Link::kNever;

    bool ShouldRegister() override;
    void OnReady() override;

    void Attach(std::function<bool(uint64_t)> level, std::function<uint64_t(uint64_t)> next);
    void Refresh();
    void Kick();
    void PostRestore();

    void OnLinkRun(uint64_t) override { Kick(); }
    void OnLinkStop(uint64_t) override { Kick(); }
    void OnFifoReset(uint64_t, bool) override { Kick(); }
    void OnCodecWrite(uint64_t) override { Kick(); }
    void OnLinkEvent() override { Kick(); }

private:
    void Drive(bool level);
    void ResetLine();

    GuestCycleClock*        clock_ = nullptr;
    IrqController*          intc_  = nullptr;
    GuestCycleClock::Event* ev_    = nullptr;
    std::function<bool(uint64_t)>     level_;
    std::function<uint64_t(uint64_t)> next_;
    bool line_ = false;
};
