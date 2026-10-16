#include <cerfcore/host/keyboard_input.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/host/keyboard_map.h>
#include <cerfcore/host/keyboard_router.h>
#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_900_series_family_id.h"
#include "nec_mobilepro_900_series_pco_companion.h"

#include <cstdint>
#include <mutex>

namespace {

class NecMobilepro900SeriesKeyboardInput : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecMobilepro900Series;
    }

    void OnReady() override {
        emu_.Get<KeyboardRouter>().Register(this);
    }

    void OnHostKey(uint8_t vk, bool key_up) override {
        LOG(Board, "[NEC-KEY] vk=0x%02X up=%d\n", vk, key_up ? 1 : 0);
        uint32_t code;
        if (!emu_.Get<KeyboardMap>().BaseDeviceCode(vk, code)) return;
        const uint8_t bit  = static_cast<uint8_t>(code);
        const uint8_t byte = bit >> 3;
        const uint8_t mask = static_cast<uint8_t>(1u << (bit & 7u));
        NecMobilepro900SeriesPcoCompanion::KeyMatrix state;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (key_up) live_[byte] |= mask;
            else        live_[byte] &= static_cast<uint8_t>(~mask);
            state = live_;
        }
        emu_.Get<NecMobilepro900SeriesPcoCompanion>().QueueKeyState(state);
    }

private:
    std::mutex                                   mtx_;
    NecMobilepro900SeriesPcoCompanion::KeyMatrix live_ =
        NecMobilepro900SeriesPcoCompanion::kIdleMatrix;
};

}

REGISTER_SERVICE(NecMobilepro900SeriesKeyboardInput);
