#include "omap3530_gpio_bus.h"
#include "omap3530_prcm_stub_block.h"

#include <cerfcore/boards/board_context.h>
#include "omap3530_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>

namespace cerfcore {

REGISTER_SERVICE(Omap3530GpioBus);

bool Omap3530GpioBus::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Omap3530;
}

void Omap3530GpioBus::RegisterBank(uint32_t bank_index,
                                   Omap3530GpioBankBase* bank) {
    if (bank_index >= kBankCount) {
        LOG(Caution, "Omap3530GpioBus::RegisterBank bank_index=%u out of range\n",
            bank_index);
        return;
    }
    banks_[bank_index] = bank;
}

void Omap3530GpioBus::SetInputPin(uint32_t absolute_pin, bool high) {
    const uint32_t bank_index = absolute_pin / 32u;
    const uint32_t bit        = absolute_pin % 32u;
    if (bank_index >= kBankCount) return;
    Omap3530GpioBankBase* bank = banks_[bank_index];
    if (!bank) return;
    bank->SetInputPin(bit, high);
}

std::optional<bool> Omap3530GpioBus::DrivenLevel(uint32_t absolute_pin) {
    const uint32_t bank_index = absolute_pin / 32u;
    if (bank_index >= kBankCount || banks_[bank_index] == nullptr) {
        emu_.Get<Fatal>().Die("Omap3530GpioBus: no GPIO bank holds pin %u", absolute_pin);
    }
    return banks_[bank_index]->DrivenLevel(absolute_pin % 32u);
}

}
