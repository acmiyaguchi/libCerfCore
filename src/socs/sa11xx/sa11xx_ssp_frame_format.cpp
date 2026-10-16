#include "sa11xx_ssp_frame_format.h"

#include "sa11xx_ssp_regs.h"

using namespace sa11xx_ssp;

Sa11xxSspFrameFormat Sa11xxSspFrameFormat::Of(uint32_t sscr0, uint32_t sscr1) {
    const uint64_t n = (sscr0 & kDssMask) + 1u;
    Sa11xxSspFrameFormat f;
    f.data_bits = static_cast<uint32_t>(n);
    /* SSDR (page 11-163): "the user should left justify data written to the transmit FIFO". */
    f.tx_bits  = static_cast<uint32_t>(n);
    f.tx_shift = 16u - static_cast<uint32_t>(n);
    switch ((sscr0 >> kFrfShift) & 0x3u) {
        /* §11.12.7.1 Figure 11-34 (page 11-153): "the SFRM pin is pulled high one SCLK period after the
           last bit has been latched", "the MSB of the next frame follows directly after the LSB";
           continuous words reach the receive FIFO "on the falling edge SCLK after the LSB". */
        case kFrfMotorola: {
            /* §11.12.10.5 (page 11-160): "When SPO and SPH are the same value ... receive data is
               latched on the rising edge of SCLK", opposite values latch "on the falling edge". */
            const bool rising = ((sscr1 & kSpo) != 0u) == ((sscr1 & kSph) != 0u);
            f.period    = 2u * n;
            f.push_cont = 2u * n + (rising ? 1u : 2u);
            f.push_last = 2u * n + 2u;
            break;
        }
        /* Figure 11-35 (page 11-154): an 8-bit control word, a one-clock wait, then the 4- to 16-bit
           reply; "the control byte of the next frame follows directly after the LSB of the received
           data". */
        case kFrfMicrowire:
            /* SSDR (page 11-163): for Microwire "the default size for transmit data is 8 bits (the
               most significant byte is ignored)". */
            f.tx_bits   = 8u;
            f.tx_shift  = 0u;
            f.period    = 2u * n + 18u;
            f.push_cont = 2u * n + 19u;
            f.push_last = 2u * n + 20u;
            break;
        /* Figure 11-33 (page 11-152): the received data "is transferred ... to the receive FIFO on the
           first rising edge of SCLK after the LSB has been latched"; back-to-back frames put the next
           SFRM pulse over bit 0. */
        default:
            f.period    = 2u * n;
            f.push_cont = 2u * n + 2u;
            f.push_last = 2u * n + 2u;
            break;
    }
    return f;
}
