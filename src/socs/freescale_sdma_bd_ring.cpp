#include "freescale_sdma_bd_ring.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/state/state_stream.h>

#include <string>

using cerf_freescale_sdma_detail::kBdCont;
using cerf_freescale_sdma_detail::kBdDone;
using cerf_freescale_sdma_detail::kBdWrap;

/* MCIMX51RM page 52-231: "the next time the channel will be triggered, the SDMA script will open
   the BD pointed by the currentBDptr pointer of the CCB"; Figure 52-80 (page 52-229): the CCB
   words are currentBDptr, baseBDptr, chanDesc, status. */
void FreescaleSdmaBdRing::Begin(const FreescaleSdmaChannelStart& start) {
    const uint8_t* ccb = emu_.Get<EmulatedMemory>().TryTranslateWrite(start.ccb_pa);
    channel_ = start.channel;
    ccb_pa_  = start.ccb_pa;
    base_    = start.base_bd_pa;
    stride_  = start.stride;
    cur_pa_  = cerf::le::U32(ccb);
}

/* MCIMX51RM Table 52-96 D: "when D=1 SDMA owns the buffer descriptor"; Count: "the size of the
   data to be transmitted". */
bool FreescaleSdmaBdRing::Open(FreescaleSdmaBd& bd) const {
    const uint8_t* raw = emu_.Get<EmulatedMemory>().TryTranslateWrite(cur_pa_);
    if (raw == nullptr) return false;
    const uint32_t word0 = cerf::le::U32(raw);
    if ((word0 & kBdDone) == 0u) return false;
    bd.pa     = cur_pa_;
    bd.buffer = cerf::le::U32(raw, 4);
    bd.count  = word0 & 0xFFFFu;
    bd.cont   = (word0 & kBdCont) != 0u;
    return true;
}

/* MCIMX51RM page 52-230 Table 52-96 W: "When encountering this bit set, the SDMA scripts updates
   the CurrentBD pointer to point to the first Buffer Descriptor of the array"; C: "decoded at the
   end of the processing of a BD to determine if the SDMA script must open a new BD". */
bool FreescaleSdmaBdRing::Retire(FreescaleSdmaChannelHost& host, bool cont) {
    host.SignalChannelBdDone(channel_, cur_pa_);
    auto&          mem  = emu_.Get<EmulatedMemory>();
    const bool     wrap = (cerf::le::U32(mem.TryTranslateWrite(cur_pa_)) & kBdWrap) != 0u;
    cur_pa_ = wrap ? base_ : cur_pa_ + stride_;
    cerf::le::Put32(mem.TryTranslateWrite(ccb_pa_), cur_pa_);
    return cont || host.EndChannelRun(channel_);
}

void FreescaleSdmaBdRing::Save(StateWriter& w, const char* prefix) const {
    const std::string p(prefix);
    w.Write((p + "bd_channel").c_str(), channel_);
    w.Write((p + "bd_ccb").c_str(), ccb_pa_);
    w.Write((p + "bd_base").c_str(), base_);
    w.Write((p + "bd_stride").c_str(), stride_);
    w.Write((p + "bd_current").c_str(), cur_pa_);
}

void FreescaleSdmaBdRing::Restore(StateReader& r, const char* prefix) {
    const std::string p(prefix);
    r.Read((p + "bd_channel").c_str(), channel_);
    r.Read((p + "bd_ccb").c_str(), ccb_pa_);
    r.Read((p + "bd_base").c_str(), base_);
    r.Read((p + "bd_stride").c_str(), stride_);
    r.Read((p + "bd_current").c_str(), cur_pa_);
}
