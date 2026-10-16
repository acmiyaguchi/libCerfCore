#include "omap3530_mcbsp_direction.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>
#include "omap3530_id.h"

#include <vector>

template <uint32_t kPort>
bool Omap3530McBspDirection<kPort>::ShouldRegister() {
    return emu_.Get<BoardContext>().GetSocId() == SocId::Omap3530;
}

template <uint32_t kPort>
void Omap3530McBspDirection<kPort>::InitDirection(Dir dir, uint32_t source, int irq) {
    clock_ = &emu_.Get<Clock>();
    InitRequestLines(source, dir, irq);
    clock_->SetListener(dir, this);
}

template <uint32_t kPort>
void Omap3530McBspDirection<kPort>::Revalidate(uint64_t now) {
    if (enabled_) clock_->RequireRunnable(Direction(), now);
    RefreshLines(now);
}

template <uint32_t kPort>
void Omap3530McBspDirection<kPort>::OnTimingRescaled(uint64_t now) {
    Revalidate(now);
}

template <uint32_t kPort>
void Omap3530McBspDirection<kPort>::SaveWords(StateWriter& w, const std::deque<uint32_t>& words) const {
    const bool                  rx = Direction() == Dir::Rx;
    const std::vector<uint32_t> flat(words.begin(), words.end());
    w.Write<uint32_t>(rx ? "mcbsp_rx_words" : "mcbsp_tx_words", static_cast<uint32_t>(flat.size()));
    w.WriteBytes(rx ? "mcbsp_rx_buffer" : "mcbsp_tx_buffer", flat.data(), flat.size() * sizeof(uint32_t));
}

template <uint32_t kPort>
void Omap3530McBspDirection<kPort>::RestoreWords(StateReader& r, std::deque<uint32_t>& words) const {
    const bool rx    = Direction() == Dir::Rx;
    uint32_t   count = 0;
    r.Read(rx ? "mcbsp_rx_words" : "mcbsp_tx_words", count);
    std::vector<uint32_t> flat(count);
    r.ReadBytes(rx ? "mcbsp_rx_buffer" : "mcbsp_tx_buffer", flat.data(), flat.size() * sizeof(uint32_t));
    words.assign(flat.begin(), flat.end());
}

template class Omap3530McBspDirection<1>;
template class Omap3530McBspDirection<2>;
template class Omap3530McBspDirection<3>;
template class Omap3530McBspDirection<4>;
template class Omap3530McBspDirection<5>;
