#pragma once

#include <cerfcore/core/service.h>
#include "freescale_sdma_audio_player.h"
#include "freescale_ssi_slot_grid.h"

#include <cstdint>

namespace cerfcore {

class FreescaleSsiExternalClock : public Service {
public:
    using Service::Service;

    virtual FreescaleAudioFormat Format(uint32_t ssi) const = 0;
    virtual uint32_t             FrameSyncBitClocks(uint32_t ssi) const = 0;

    FreescaleSsiFrameShape Shape(uint32_t ssi) const;
};

}
