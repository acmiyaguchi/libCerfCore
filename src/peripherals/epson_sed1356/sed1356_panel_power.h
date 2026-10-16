#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <mutex>

class Sed1356PowerSequence;
class StateReader;
class StateWriter;

class Sed1356PanelPower : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void    PowerUp();
    void    PowerDown();
    uint8_t StatusBits(uint8_t mode_reg, uint8_t power_save_reg, uint8_t refresh_reg);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    enum class State : uint8_t { Down = 0, Up = 1 };

    const Sed1356PowerSequence* seq_ = nullptr;
    std::mutex                  mtx_;
    State                       state_ = State::Down;
};
