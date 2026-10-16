#pragma once

#include <cerfcore/core/service.h>

#include <array>
#include <cstdint>

class Imx31ClockInput;
class StateReader;
class StateWriter;

/* MCIMX31RM §3.2.1.3 PLL Reference Clock Switch Unit; §3.2.2 "three DPLLs ... from the PLL
   reference clock". */
class Imx31Plls : public Service {
public:
    using Service::Service;

    static constexpr uint32_t kCount = 3u;
    using Controls = std::array<uint32_t, kCount>;

    bool ShouldRegister() override;
    void OnReady() override;

    void     Settle(uint32_t ccmr, const Controls& ctl);
    void     OnCcmrWrite(uint32_t old, uint32_t value, const Controls& ctl);
    void     OnControlWrite(uint32_t pll, uint32_t old, uint32_t value, uint32_t ccmr);
    uint64_t RefHz() const;
    uint64_t OutputHz(uint32_t pll, uint64_t off_hz) const;
    bool     Running(uint32_t pll) const { return on_[pll] != 0u; }

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    static uint32_t Prcs(uint32_t ccmr);

    uint64_t LockedHz(uint32_t pll) const;

    void Lock(uint32_t pll, uint32_t ctl);
    void SwitchReference(uint32_t prcs);

    const Imx31ClockInput*      input_ = nullptr;
    Controls                    ctl_{};
    std::array<uint8_t, kCount> on_{};
    uint8_t                     prcs_  = 0u;
};
