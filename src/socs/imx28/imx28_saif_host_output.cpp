#include "imx28_saif_host_output.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "imx28_id.h"

namespace cerfcore {

REGISTER_SERVICE(Imx28SaifHostOutput);

bool Imx28SaifHostOutput::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

void Imx28SaifHostOutput::OnReady() { session_.Open(); }

void Imx28SaifHostOutput::OnShutdown() { session_.Close(); }

}
