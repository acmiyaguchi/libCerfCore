#include "ucb1x00_codec.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/hp_jornada_820/hp_jornada_820_family_id.h"
#include "../../boards/philips_nino/philips_nino_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

#include <cstdint>

namespace cerfcore {

namespace {

/* NetBSD hpcmips ucb1200reg.h UCB1200_ID 0x1004 (version 4, device 0, supplier 1). */
constexpr uint16_t kIdUcb1200 = 0x1004u;

/* UCB1200 datasheet p.49: TEL_DIV resets to 16, AUD_DIV to 6. */
constexpr uint16_t kResetTelDiv = 16u;
constexpr uint16_t kResetAudDiv = 6u;

class Ucb1200 : public Ucb1x00Codec {
public:
    using Ucb1x00Codec::Ucb1x00Codec;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd) return false;
        const std::string_view b = bd->GetBoardFamilyId();
        return b == BoardFamilyId::HpJornada820 || b == BoardFamilyId::PhilipsNino;
    }

protected:
    uint16_t DeviceId() const override { return kIdUcb1200; }

    std::array<uint16_t, 16> PowerOnRegs() const override {
        std::array<uint16_t, 16> r{};
        r[5] = kResetTelDiv;
        r[7] = kResetAudDiv;
        return r;
    }
};

}

REGISTER_SERVICE_AS(Ucb1200, Ucb1x00Codec);

}
