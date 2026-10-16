#pragma once

#include <cerfcore/core/service.h>
#include <cerfcore/host/widget_control.h>
#include "../pcmcia/pcmcia_card_catalog.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cerfcore {

class CompactFlashInsertEntry : public Service {
public:
    using Service::Service;

    SlotEntry BuildInsertEntry(PcmciaCardCatalog::CardInserter inserter);

private:
    std::string CfImgPath() const;
    std::string ChooseImageFile() const;
    std::vector<std::string> ChooseFilesToGenerate() const;
    uint32_t PayloadMb(const std::vector<std::string>& files) const;
    uint32_t PromptSizeMb(uint32_t min_mb) const;
};

}
