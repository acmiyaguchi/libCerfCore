#pragma once

#include <cerfcore/core/service.h>
#include <cerfcore/host/widget_control.h>
#include "../pcmcia/pcmcia_card_catalog.h"

class HpPalmtopVgaInsertEntry : public Service {
public:
    using Service::Service;

    SlotEntry BuildInsertEntry(PcmciaCardCatalog::CardInserter inserter);
};
