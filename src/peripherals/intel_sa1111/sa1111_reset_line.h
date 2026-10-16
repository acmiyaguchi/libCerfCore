#pragma once

#include <cerfcore/core/service.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

class StateReader;
class StateWriter;

class Sa1111ResetLine : public Service {
public:
    using Service::Service;

    enum class Pin { Low, High, Floating };

    bool ShouldRegister() override;
    void OnReady() override;

    void RegisterListener(std::function<void(bool held)> fn);
    void Drive(Pin level, bool resync);
    void OnSkcrWrite(bool rclk_enabled);
    bool Held() const { return held_.load(std::memory_order_acquire); }
    bool NresetAsserted() const { return pin_ == Pin::Low; }

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    void Assert();
    void Release();

    std::vector<std::function<void(bool)>> listeners_;
    Pin               pin_  = Pin::Floating;
    std::atomic<bool> held_{false};
    bool              power_on_pending_ = false;
};
