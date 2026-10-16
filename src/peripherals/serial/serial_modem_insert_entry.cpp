#include "serial_modem_insert_entry.h"

#include <cerfcore/core/cerf_emulator.h>

#include <utility>

namespace {

constexpr const char* kGettingOnlineUrl = "https://cerf.cx/getting-online";

}

REGISTER_SERVICE(SerialModemInsertEntry);

SlotEntry SerialModemInsertEntry::BuildInsertEntry(std::function<void()> on_insert) {
    SlotEntry entry;
    entry.insert  = std::move(on_insert);
    entry.details = {WidgetControl{LinkControl{"Getting online tutorial", kGettingOnlineUrl}}};
    return entry;
}
