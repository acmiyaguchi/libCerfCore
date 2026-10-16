#include "freescale_ssi_external_clock.h"

namespace cerfcore {

/* MCIMX51RM page 56-16 and MCIMX31RM page 45-16: in I2S slave mode "each frame sync transition is the start of
   a new frame"; "After one data word has been transferred, the SSI waits for the next frame sync transition". */
FreescaleSsiFrameShape FreescaleSsiExternalClock::Shape(uint32_t ssi) const {
    const FreescaleAudioFormat format = Format(ssi);
    const uint32_t             bits   = FrameSyncBitClocks(ssi);
    FreescaleSsiFrameShape     shape;
    shape.bit_rate = GuestCycleClock::Rate{static_cast<uint64_t>(format.channels) * format.rate_hz * bits, 1u};
    shape.slots    = 1u;
    shape.bits     = bits;
    return shape;
}

}
