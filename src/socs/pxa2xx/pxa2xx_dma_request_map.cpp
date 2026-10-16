#include "pxa2xx_dma_request_map.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

namespace {

/* Intel PXA255 Developer's Manual Table 5-8 (page 5-20) DRCMR: MAPVLD bit 7, CHLNUM; Intel PXA27x
   Developer's Manual Table 5-11 (page 5-31): MAPVLD bit 7, CHLNUM 4:0. */
constexpr uint32_t kMapValid    = 1u << 7;
constexpr uint32_t kChannelMask = 0x1Fu;

}  // namespace

void Pxa2xxDmaRequestMap::RegisterPort(uint32_t request, Pxa2xxDmaPort* port) {
    if (request >= kMaxRequests || ports_[request] != nullptr) {
        emu_.Get<Fatal>().Die("Pxa2xxDma: DMA request %u registered twice", request);
    }
    ports_[request] = port;
}

bool Pxa2xxDmaRequestMap::MapsChannel(uint32_t request, uint32_t& ch) const {
    const uint32_t v = DrcmrOf(request);
    if ((v & kMapValid) == 0u) return false;
    ch = v & kChannelMask;
    return true;
}

Pxa2xxDmaPort* Pxa2xxDmaRequestMap::MappedPortOf(uint32_t request, uint32_t& ch) const {
    if (ports_[request] == nullptr || !MapsChannel(request, ch)) return nullptr;
    return ports_[request];
}

bool Pxa2xxDmaRequestMap::AnyRequestMapsLocked(uint32_t ch, uint32_t& request) const {
    for (uint32_t r = 0; r < kMaxRequests; ++r) {
        uint32_t mapped = 0;
        if (!MapsChannel(r, mapped) || mapped != ch) continue;
        request = r;
        return true;
    }
    return false;
}

/* Intel PXA255 Developer's Manual Table 5-8 (page 5-20) CHLNUM: "Do not map two active requests to the
   same channel. It produces unpredictable results." */
uint32_t Pxa2xxDmaRequestMap::SoleRequestOfLocked(uint32_t ch) {
    uint32_t found = kNoRequest;
    for (uint32_t r = 0; r < kMaxRequests; ++r) {
        uint32_t mapped = 0;
        if (!MapsChannel(r, mapped) || mapped != ch) continue;
        if (found != kNoRequest) DieTwoRequests(ch, found, r);
        found = r;
    }
    return found;
}

void Pxa2xxDmaRequestMap::DieTwoRequests(uint32_t ch, uint32_t a, uint32_t b) {
    emu_.Get<Fatal>().Die("Pxa2xxDma ch%u: DMA requests %u and %u both map to the channel; not modelled", ch, a,
                          b);
}
