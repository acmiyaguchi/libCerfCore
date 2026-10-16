#include "sed1356_panel_power.h"

#include "sed1356_config.h"
#include "sed1356_power_sequence.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

bool Sed1356PanelPower::ShouldRegister() {
    return emu_.TryGet<Sed1356Config>() != nullptr;
}

void Sed1356PanelPower::OnReady() {
    seq_ = &emu_.Get<Sed1356PowerSequence>();
}

void Sed1356PanelPower::PowerUp() {
    std::lock_guard<std::mutex> lk(mtx_);
    state_ = State::Up;
}

void Sed1356PanelPower::PowerDown() {
    std::lock_guard<std::mutex> lk(mtx_);
    state_ = State::Down;
}

/* SED1356 REG[1F1h] p.170-171, S1D13806 REG[1F1h] p.145: bit 1 LCD Power Save Status, 1 =
   panel powered down; bit 0 Memory Controller Power Save Status. */
uint8_t Sed1356PanelPower::StatusBits(uint8_t mode_reg, uint8_t power_save_reg,
                                      uint8_t refresh_reg) {
    std::lock_guard<std::mutex> lk(mtx_);
    const uint8_t memory =
        (power_save_reg & 0x1u) != 0u && seq_->MemoryControllerPowersDown(refresh_reg) ? 0x01u
                                                                                       : 0x00u;
    if ((mode_reg & 0x1u) == 0u && seq_->LcdDisabledReadsPanelDown()) {
        return static_cast<uint8_t>(0x02u | memory);
    }
    return state_ == State::Down ? static_cast<uint8_t>(0x02u | memory) : memory;
}

void Sed1356PanelPower::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(mtx_);
    w.Write<uint8_t>("panel_state", static_cast<uint8_t>(state_));
}

void Sed1356PanelPower::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(mtx_);
    uint8_t state = 0;
    r.Read("panel_state", state);
    state_ = static_cast<State>(state);
}

REGISTER_SERVICE(Sed1356PanelPower);

}
