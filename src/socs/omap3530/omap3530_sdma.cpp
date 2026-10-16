#include "omap3530_sdma.h"

#include <cerfcore/boards/board_context.h>
#include "omap3530_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

namespace cerfcore {

bool Omap3530Sdma::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Omap3530;
}

void Omap3530Sdma::RegisterRequester(uint32_t source, Omap3530SdmaRequester* requester) {
    if (Requester(source) != nullptr) {
        emu_.Get<Fatal>().Die("Omap3530Sdma: a second requester registered for DMA request %u", source);
    }
    requesters_.emplace_back(source, requester);
}

Omap3530SdmaRequester* Omap3530Sdma::Requester(uint32_t source) const {
    for (const auto& r : requesters_) {
        if (r.first == source) return r.second;
    }
    return nullptr;
}

/* OMAP35x TRM SPRUF98Y section 9.4.1 (page 991): a hardware-synchronized logical channel becomes
   active "when the logical channel is enabled and the hardware DMA request line is asserted". */
bool Omap3530Sdma::SyncRequestAsserted(uint32_t source) {
    Omap3530SdmaRequester* r = Requester(source);
    return r != nullptr && r->RequestAsserted();
}

void Omap3530Sdma::OnSyncServed(uint32_t source) {
    if (Omap3530SdmaRequester* r = Requester(source)) r->Served();
}

REGISTER_SERVICE(Omap3530Sdma);

}
