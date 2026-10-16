#include "sa11xx_mcp_audio_stream.h"

#include <cerfcore/core/cerf_emulator.h>
#include "sa11xx_mcp_audio_receive_path.h"
#include "sa11xx_mcp_audio_transmit_path.h"
#include "sa11xx_mcp_regs.h"

namespace cerfcore {

using namespace sa11xx_mcp;

namespace {

Sa11xxMcpChannel::Config AudioConfig() {
    Sa11xxMcpChannel::Config c;
    /* SA-1110 Table 11-6 (printed 11-9): MCP transmit (audio) DA 0x818002, DS 1010; Linux sa11x0-dma
       DDAR_Ser4MCP0Tr (0xa << 4), DDAR_Ser4MCP0Rc (0xb << 4). */
    c.device_address = 0x818002u;
    c.ds_transmit    = 0xAu;
    c.ds_receive     = 0xBu;
    /* §11.12.3 MCCR0 (printed 11-137): ASD bits 6..0, "Value (from 6 to 127)". */
    c.divisor_shift  = 0u;
    c.divisor_min    = 6u;
    c.low_rate_hz    = 11025u;
    /* Linux ucb1x00 UCB_AC_B 0x08; §11.12.6.15 (printed 11-148): "Audio Control Register B
       (register 8)". */
    c.codec_reg       = 8u;
    c.transmit_enable = kAte;
    c.receive_enable  = kAre;
    c.ts = kAts;
    c.rs = kArs;
    c.tu = kAtu;
    c.ro = kAro;
    c.nf = kAnf;
    c.ne = kAne;
    c.ce = kAce;
    return c;
}

}

Sa11xxMcpAudioStream::Sa11xxMcpAudioStream(CerfEmulator& emu)
    : Sa11xxMcpChannel(emu, AudioConfig()) {}

void Sa11xxMcpAudioStream::OnReady() {
    AttachPaths(&emu_.Get<Sa11xxMcpAudioTransmitPath>(), &emu_.Get<Sa11xxMcpAudioReceivePath>());
    Sa11xxMcpChannel::OnReady();
}

REGISTER_SERVICE(Sa11xxMcpAudioStream);

}
