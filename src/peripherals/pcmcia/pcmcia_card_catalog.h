#pragma once

#include <cerfcore/core/service.h>
#include "pcmcia_card.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

/* The set of PC Card types CERF can insert into any slot at runtime.
   Slot "Insert card" menus list Entries(); the chosen id goes through
   Create(). */
class PcmciaCardCatalog : public Service {
public:
    using Service::Service;

    using CardInserter = std::function<void(std::unique_ptr<PcmciaCard>)>;

    struct Entry {
        std::string                            id;
        std::string                            display_name;
        std::function<SlotEntry(CardInserter)> build_entry;
    };

    void OnReady() override;

    const std::vector<Entry>& Entries() const { return entries_; }
    std::unique_ptr<PcmciaCard> Create(const std::string& id,
                                       const std::string& binding = "");
    std::unique_ptr<PcmciaCard> TryCreate(const std::string& id,
                                          const std::string& binding = "");

private:
    std::vector<Entry> entries_;
};
