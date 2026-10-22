#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace cerfcore {

/* MCIMX28RM Rev. 2 section 38.1, Table 38-1, Figure 38-2: the analog quantities behind the LRADC
   inputs. */
class Imx28LradcInputs : public Service {
public:
    using Service::Service;

    virtual uint32_t PinMillivolts(uint32_t pin, uint32_t ctrl0, uint32_t ctrl2) = 0;
    virtual uint32_t BatteryMillivolts() = 0;
    virtual uint32_t Vdd5vMillivolts() = 0;
    virtual bool     TouchDetected(uint32_t ctrl0) = 0;
    virtual bool     ButtonDetected(uint32_t button, uint32_t ctrl0) = 0;

    void RegisterChangeListener(std::function<void()> fn) { listeners_.push_back(std::move(fn)); }

protected:
    void NotifyChanged() {
        for (auto& fn : listeners_) fn();
    }

private:
    std::vector<std::function<void()>> listeners_;
};

}
