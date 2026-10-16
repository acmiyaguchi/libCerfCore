#include "../../peripherals/mediaq_mq200/mediaq_mq200_config.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "nec_cronos3e_family_id.h"

namespace {

class NecCronos3eMq200Config : public MediaQMq200Config {
public:
    using MediaQMq200Config::MediaQMq200Config;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecCronos3e;
    }

    /* NetBSD hpcmips GENERIC config: mqvideo at vr4102ip addr 0x0a000000; nec_mobilepro_790_hpc2000
       nk.exe sub_9E09CA30 stores DC00R at kseg1 0xAA614000. */
    uint32_t HostWindowBase() const override { return 0x0A000000u; }

    /* NetBSD hpcmips mq200 machdep_params, MCR-530A row: base clock 12288, OSCFREQ_12_25;
       nec_mobilepro_790_hpc2000 nk.exe sub_9E09CA30 programs DC00R[27:26] = 11. */
    uint32_t OscillatorHz() const override { return 12288000u; }

    uint32_t BusInterfaceMode() const override { return 0x04u; }
};

}

REGISTER_SERVICE_AS(NecCronos3eMq200Config, MediaQMq200Config);
