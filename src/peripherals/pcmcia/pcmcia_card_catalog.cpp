#include "pcmcia_card_catalog.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include "../compactflash/compactflash_card.h"
#include "../compactflash/compactflash_insert_entry.h"
#include "../hp_palmtop_vga/hp_palmtop_vga_card.h"
#include "../hp_palmtop_vga/hp_palmtop_vga_insert_entry.h"
#include "../realtek_rtl8019/rtl8019.h"
#include "../serial/serial_forward_insert_entry.h"
#include "../serial/serial_modem_insert_entry.h"
#include "../serial_pccard/serial_pccard.h"

namespace cerfcore {

namespace {

const char kIdNe2000[]    = "ne2000";
const char kIdHpVga[]     = "hpvga";
const char kIdSerial[]    = "serial";
const char kIdSerialFwd[] = "serial_fwd";

}  /* namespace */

void PcmciaCardCatalog::OnReady() {
    Entry ne2000;
    ne2000.id           = kIdNe2000;
    ne2000.display_name = Rtl8019::kDisplayName;

    Entry cf;
    cf.id           = "cf";
    cf.display_name = "Compact Flash";
    cf.build_entry = [this](CardInserter inserter) {
        return emu_.Get<CompactFlashInsertEntry>().BuildInsertEntry(std::move(inserter));
    };

    Entry hpvga;
    hpvga.id           = kIdHpVga;
    hpvga.display_name = HpPalmtopVgaCard::kDisplayName;
    hpvga.build_entry = [this](CardInserter inserter) {
        return emu_.Get<HpPalmtopVgaInsertEntry>().BuildInsertEntry(std::move(inserter));
    };

    Entry serial;
    serial.id           = kIdSerial;
    serial.display_name = SerialPcCard::kDisplayName;
    serial.build_entry = [this](CardInserter inserter) {
        return emu_.Get<SerialModemInsertEntry>().BuildInsertEntry(
            [this, inserter] { inserter(std::make_unique<SerialPcCard>(emu_)); });
    };

    Entry serial_fwd;
    serial_fwd.id           = kIdSerialFwd;
    serial_fwd.display_name = SerialPcCard::kForwardDisplayName;
    serial_fwd.build_entry = [this](CardInserter inserter) {
        return emu_.Get<SerialForwardInsertEntry>().BuildInsertEntry(
            [this, inserter](std::string port) {
                inserter(std::make_unique<SerialPcCard>(emu_, std::move(port)));
            });
    };

    entries_.push_back(std::move(ne2000));
    entries_.push_back(std::move(cf));
    entries_.push_back(std::move(hpvga));
    entries_.push_back(std::move(serial));
    entries_.push_back(std::move(serial_fwd));
}

std::unique_ptr<PcmciaCard> PcmciaCardCatalog::TryCreate(const std::string& id,
                                                         const std::string& binding) {
    if (id == kIdNe2000)    return std::make_unique<Rtl8019>(emu_);
    if (id == "cf")         return std::make_unique<CompactFlashCard>(emu_, binding);
    if (id == kIdHpVga)     return std::make_unique<HpPalmtopVgaCard>(emu_);
    if (id == kIdSerial)    return std::make_unique<SerialPcCard>(emu_);
    if (id == kIdSerialFwd) return std::make_unique<SerialPcCard>(emu_, binding);
    return nullptr;
}

std::unique_ptr<PcmciaCard> PcmciaCardCatalog::Create(const std::string& id,
                                                      const std::string& binding) {
    auto card = TryCreate(id, binding);
    if (!card)
        emu_.Get<Fatal>().Die("PcmciaCardCatalog::Create: unknown card id '%s'", id.c_str());
    return card;
}

REGISTER_SERVICE(PcmciaCardCatalog);

}
