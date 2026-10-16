#include "ucb1x00_codec.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/odo/odo_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

#include <cstdint>

namespace {

constexpr uint16_t kAdcCrPollBits = 0x0C00u;

constexpr uint8_t  kRegAdcCr       = 10u;
constexpr uint8_t  kRegMode        = 13u;
constexpr uint16_t kAdcCrStubBits  = 0x3F00u;
constexpr uint16_t kModeStubBits   = 0x0280u;

class OdoUcbCodec : public Ucb1x00Codec {
public:
    using Ucb1x00Codec::Ucb1x00Codec;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::Odo;
    }

protected:
    uint16_t DeviceId() const override {
        emu_.Get<Fatal>().Die("odo ucb: ID register read; the codec part on this "
                              "board is not identified");
    }

    std::array<uint16_t, 16> PowerOnRegs() const override { return {}; }

    uint16_t AdcCrReadBack(uint16_t written) const override {
        return static_cast<uint16_t>(written | kAdcCrPollBits);
    }

    uint16_t StubWriteBits(uint8_t reg) const override {
        if (reg == kRegAdcCr) return kAdcCrStubBits;
        if (reg == kRegMode)  return kModeStubBits;
        return 0u;
    }
};

}

REGISTER_SERVICE_AS(OdoUcbCodec, Ucb1x00Codec);
