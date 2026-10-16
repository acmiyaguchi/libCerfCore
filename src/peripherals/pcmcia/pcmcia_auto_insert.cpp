#include "pcmcia_auto_insert.h"

#include <cerfcore/boot/rom_parser_queries.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/log.h>
#include <cerfcore/host/configuration_problem.h>
#include <cerfcore/platform/host_file.h>
#include "../compactflash/compactflash_card.h"
#include "pcmcia_card_catalog.h"
#include "pcmcia_slot.h"

#include <cstdint>
#include <memory>
#include <string>

namespace cerfcore {

REGISTER_SERVICE(PcmciaAutoInsert);

void PcmciaAutoInsert::InsertDefaultNetworkCard(PcmciaSlot& slot) {
    const auto& cfg = emu_.Get<DeviceConfig>();
    if (cfg.guest_additions && cfg.network_enabled) {
        LOG(Net, "PcmciaAutoInsert: guest additions provide the NIC; leaving "
                 "'%s' empty\n", slot.WidgetName().c_str());
        return;
    }

    auto* rom = emu_.TryGet<RomParserQueries>();
    uint16_t major = 0, minor = 0;
    if (!rom || !rom->KernelSubsystemVersion(major, minor)) {
        LOG(Net, "PcmciaAutoInsert: guest kernel version unavailable; "
                 "leaving '%s' empty\n", slot.WidgetName().c_str());
        return;
    }
    if (major < 4) {
        LOG(Net, "PcmciaAutoInsert: guest kernel is CE %u.%u; leaving "
                 "'%s' empty\n", major, minor, slot.WidgetName().c_str());
        return;
    }

    slot.InsertCard(emu_.Get<PcmciaCardCatalog>().Create("ne2000"));
}

void PcmciaAutoInsert::InsertLaunchCompactFlash(PcmciaSlot& slot) {
    const auto& cfg = emu_.Get<DeviceConfig>();
    for (const auto& card : cfg.bundled_compact_flash_cards) {
        if (!card.insert_on_launch) continue;

        const std::string path = GetDeviceDir(cfg.device_name) + card.file;

        if (!host_platform::HostPathExists(path)) {
            LOG(Caution, "PcmciaAutoInsert: launch CF '%s' missing at '%s'\n",
                card.name.c_str(), path.c_str());
            emu_.Get<ConfigurationProblem>().LaunchCompactFlashMissing(card.name);
            return;
        }

        slot.InsertCard(std::make_unique<CompactFlashCard>(emu_, path));
        LOG(Periph, "PcmciaAutoInsert: inserted launch CF '%s' into '%s'\n",
            card.name.c_str(), slot.WidgetName().c_str());
        return;
    }
}

}
