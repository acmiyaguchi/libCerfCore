#include "sed1356_power_sequence.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/hp_jornada_700/hp_jornada_700_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace cerfcore {

namespace {

class S1d13506PowerSequence : public Sed1356PowerSequence {
public:
    using Sed1356PowerSequence::Sed1356PowerSequence;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::HpJornada700;
    }

    /* SED1356 X25B-A-001-12 Table 7-20 p.78: t1 and t3 give maxima only. */
    uint32_t LcdPowerOnLines(uint8_t) const override { return 0u; }

    bool LcdDisabledReadsPanelDown() const override { return false; }

    /* Figure 7-21 p.79 note; REG[021h] bits 7-6 Table 8-12 p.132: 00 CBR refresh. */
    bool MemoryControllerPowersDown(uint8_t refresh_reg) const override {
        return (refresh_reg & 0xC0u) != 0u;
    }
};

}

REGISTER_SERVICE_AS(S1d13506PowerSequence, Sed1356PowerSequence);

}
