#include "../../socs/pr31x00/pr31x00_sib_codec.h"

#include "ucb1x00_codec.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/philips_nino/philips_nino_family_id.h"
#include "../../boards/philips_velo_1/philips_velo_1_family_id.h"
#include "../../boards/sharp_mobilon_hc/sharp_mobilon_hc_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

#include <cstdint>

namespace cerfcore {

namespace {

/* UCB1300 datasheet p.5, pin table note 6: "SIBDOUT reset state is 1 until the SIB bus is
   running." */
constexpr uint32_t kHeldCodecWord    = 0xFFFFFFFFu;
constexpr uint32_t kInputPathOffWord = 0u;

class Ucb1x00SibAdapter : public Pr31x00SibCodec {
public:
    using Pr31x00SibCodec::Pr31x00SibCodec;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd) return false;
        const std::string_view board = bd->GetBoardFamilyId();
        return board == BoardFamilyId::PhilipsNino || board == BoardFamilyId::PhilipsVelo1 ||
               board == BoardFamilyId::SharpMobilonHc;
    }

    uint16_t ReadReg(uint8_t reg) override {
        return emu_.Get<Ucb1x00Codec>().ReadReg(reg);
    }
    void WriteReg(uint8_t reg, uint16_t value) override {
        emu_.Get<Ucb1x00Codec>().WriteReg(reg, value);
    }
    bool IrqAsserted() override { return emu_.Get<Ucb1x00Codec>().IrqAsserted(); }
    uint32_t ReceivedWord(Pr31x00SibCircuit circuit, uint32_t input_word) override {
        const Ucb1x00Codec::InputState in = emu_.Get<Ucb1x00Codec>().Input(
            circuit == Pr31x00SibCircuit::Sound ? Ucb1x00Codec::Path::Audio : Ucb1x00Codec::Path::Telecom);
        if (in == Ucb1x00Codec::InputState::Held) return kHeldCodecWord;
        return in == Ucb1x00Codec::InputState::On ? input_word : kInputPathOffWord;
    }
    bool AudioOutputAudible() override { return emu_.Get<Ucb1x00Codec>().AudioOutputAudible(); }
    void RegisterBeforeChange(std::function<void()> fn) override {
        emu_.Get<Ucb1x00Codec>().RegisterBeforeChange(std::move(fn));
    }
    void SaveState(StateWriter& w) override    { emu_.Get<Ucb1x00Codec>().SaveState(w); }
    void RestoreState(StateReader& r) override { emu_.Get<Ucb1x00Codec>().RestoreState(r); }
};

}  /* namespace */

REGISTER_SERVICE_AS(Ucb1x00SibAdapter, Pr31x00SibCodec);

}
