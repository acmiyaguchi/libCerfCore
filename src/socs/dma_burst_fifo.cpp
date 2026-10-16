#include "dma_burst_fifo.h"

#include <algorithm>

namespace cerfcore {

void DmaBurstFifo::Configure(uint32_t depth, uint32_t threshold, uint32_t burst) {
    depth_     = depth;
    threshold_ = threshold;
    burst_     = burst;
}

uint64_t DmaBurstFifo::BurstsAvailable() const {
    if (burst_ == 0u) return 0u;
    if (supply_ == kUnlimited) return kUnlimited;
    return (supply_ + burst_ - 1u) / burst_;
}

/* SA-1110 Developer's Manual §11.6.1.4 / §11.6.1.6 (printed 11-12): DBTx "contains the current
   transfer count (in bytes)". */
uint64_t DmaBurstFifo::WordsIn(uint64_t bursts) const {
    const uint64_t words = bursts * burst_;
    return supply_ == kUnlimited ? words : std::min(words, supply_);
}

/* MCIMX51RM Table 56-10 (page 56-33): "Data1...Data15 are stored in the FIFO while Data16 is
   discarded". */
uint64_t DmaBurstFifo::Landing() const {
    if (depth_ <= threshold_) return burst_;
    return std::min<uint64_t>(burst_, depth_ - threshold_);
}

uint64_t DmaBurstFifo::LandedIn(uint64_t bursts) const {
    if (bursts == 0u) return 0u;
    const uint64_t last = WordsIn(bursts) - (bursts - 1u) * burst_;
    return (bursts - 1u) * Landing() + std::min(Landing(), last);
}

void DmaBurstFifo::Put() {
    if (level_ < depth_) ++level_;
}

void DmaBurstFifo::Refill(Arrivals* arrivals) {
    if (level_ > threshold_) return;
    const uint64_t bursts = std::min<uint64_t>(BurstsAvailable(),
                                               (threshold_ - level_) / std::max(burst_, 1u) + 1u);
    if (bursts == 0u) return;
    const uint64_t words = WordsIn(bursts);
    const uint32_t from  = level_;
    level_  = static_cast<uint32_t>(std::min<uint64_t>(level_ + words, depth_));
    moved_ += words;
    if (supply_ != kUnlimited) supply_ -= words;
    if (arrivals != nullptr) {
        arrivals->refill_words  += words;
        arrivals->refill_landed += level_ - from;
    }
}

uint64_t DmaBurstFifo::Take(uint64_t n, Arrivals* arrivals) {
    if (n == 0u) return 0u;
    Refill(arrivals);
    uint64_t bursts = 0u;
    if (level_ > threshold_ && burst_ != 0u) {
        const uint64_t lead = level_ - threshold_;
        if (n >= lead) bursts = std::min(BurstsAvailable(), (n - lead) / Landing() + 1u);
    }
    const uint64_t words  = WordsIn(bursts);
    const uint64_t landed = LandedIn(bursts);
    if (arrivals != nullptr && bursts != 0u) {
        arrivals->bursts     = bursts;
        arrivals->last_words = words - (bursts - 1u) * burst_;
    }
    moved_ += words;
    if (supply_ != kUnlimited) supply_ -= words;
    const uint64_t held = level_ + landed;
    if (n > held) {
        level_ = 0u;
        return n - held;
    }
    level_ = static_cast<uint32_t>(held - n);
    return 0u;
}

bool DmaBurstFifo::TakesToMove(uint64_t words, uint64_t& n) const {
    if (words <= moved_) {
        n = 0u;
        return true;
    }
    if (burst_ == 0u || level_ <= threshold_) return false;
    const uint64_t bursts = (words - moved_ + burst_ - 1u) / burst_;
    if (bursts > BurstsAvailable()) return false;
    n = (level_ - threshold_) + (bursts - 1u) * Landing();
    return true;
}

uint64_t DmaBurstFifo::TakesBeforeEmpty() const {
    const uint64_t bursts = BurstsAvailable();
    if (bursts == kUnlimited) return kUnlimited;
    return level_ + LandedIn(bursts);
}

}
