#include "vr41xx_kiu_key_pins.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../execution/engine_bridge.h"
#include <cerfengine/host_request_channel.h>
#include "../vr4102/vr4102_id.h"
#include "../vr4111/vr4111_id.h"
#include "../vr4121/vr4121_id.h"

namespace cerfcore {

bool Vr41xxKiuKeyPins::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    if (!bd) return false;
    const std::string_view soc = bd->GetSocId();
    return soc == SocId::Vr4102 || soc == SocId::Vr4111 || soc == SocId::Vr4121;
}

void Vr41xxKiuKeyPins::OnReady() {
    host_requests_ = &emu_.Get<EngineBridge>().Engine().Get<cerfengine::HostRequestChannel>();
}

void Vr41xxKiuKeyPins::Set(uint8_t matrix_index, bool pressed) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        const uint32_t reg  = matrix_index >> 4;
        const uint16_t mask = static_cast<uint16_t>(1u << (matrix_index & 15u));
        if (((held_[reg] & mask) != 0) == pressed) return;
        if (pressed) held_[reg] |= mask;
        else         held_[reg] &= static_cast<uint16_t>(~mask);
        dirty_ = true;
    }
    host_requests_->Request();
}

bool Vr41xxKiuKeyPins::Take(uint16_t (&pins)[kRegs]) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!dirty_) return false;
    dirty_ = false;
    for (uint32_t i = 0; i < kRegs; ++i) pins[i] = held_[i];
    return true;
}

void Vr41xxKiuKeyPins::Clear() {
    std::lock_guard<std::mutex> lk(mtx_);
    for (uint16_t& w : held_) w = 0;
    dirty_ = false;
}

REGISTER_SERVICE(Vr41xxKiuKeyPins);

}
