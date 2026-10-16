#include "sa11xx_mcp_telecom_stream.h"

#include <cerfcore/core/cerf_emulator.h>
#include "sa11xx_mcp_regs.h"
#include "sa11xx_mcp_telecom_receive_path.h"
#include "sa11xx_mcp_telecom_transmit_path.h"

namespace cerfcore {

using namespace sa11xx_mcp;

namespace {

Sa11xxMcpChannel::Config TelecomConfig() {
    Sa11xxMcpChannel::Config c;
    /* SA-1110 Table 11-6 (printed 11-9): MCP transmit (telecom) DA 0x818003, DS 1100, receive DS
       1101; Linux sa11x0-dma DDAR_Ser4MCP1Tr (0xc << 4), DDAR_Ser4MCP1Rc (0xd << 4). */
    c.device_address = 0x818003u;
    c.ds_transmit    = 0xCu;
    c.ds_receive     = 0xDu;
    /* §11.12.3 MCCR0 (printed 11-137): TSD bits 14..8, "Value (from 16 to 127)". */
    c.divisor_shift  = kTsdShift;
    c.divisor_min    = 16u;
    c.low_rate_hz    = 0u;
    /* Linux ucb1x00 UCB_TC_B 0x06; §11.12.6.16 (printed 11-149): "Telecom Control Register B
       (register 6)". */
    c.codec_reg       = 6u;
    c.transmit_enable = kTte;
    c.receive_enable  = kTre;
    c.ts = kTts;
    c.rs = kTrs;
    c.tu = kTtu;
    c.ro = kTro;
    c.nf = kTnf;
    c.ne = kTne;
    c.ce = kTce;
    return c;
}

}

Sa11xxMcpTelecomStream::Sa11xxMcpTelecomStream(CerfEmulator& emu)
    : Sa11xxMcpChannel(emu, TelecomConfig()) {}

void Sa11xxMcpTelecomStream::OnReady() {
    AttachPaths(&emu_.Get<Sa11xxMcpTelecomTransmitPath>(),
                &emu_.Get<Sa11xxMcpTelecomReceivePath>());
    Sa11xxMcpChannel::OnReady();
}

REGISTER_SERVICE(Sa11xxMcpTelecomStream);

}
