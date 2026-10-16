#include "../boot/boot_mode.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>

namespace {

class NullBootMode : public BootMode {
public:
    using BootMode::BootMode;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetRomPlacingMode()
            == RomPlacingMode::Unknown;
    }

    uint32_t ColdEntryPa() override { return 0; }
    uint32_t ColdStackPa() override { return 0; }
};

}  /* namespace */

REGISTER_SERVICE_AS(NullBootMode, BootMode);
