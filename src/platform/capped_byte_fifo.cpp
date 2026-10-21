#include "capped_byte_fifo.h"

#include <algorithm>

namespace cerfcore {

void CappedByteFifo::SetCap(size_t cap_bytes) {
    std::lock_guard<std::mutex> lk(mtx_);
    cap_ = cap_bytes;
}

void CappedByteFifo::Push(const uint8_t* data, size_t bytes) {
    if (bytes == 0u) return;
    std::lock_guard<std::mutex> lk(mtx_);
    bytes_.insert(bytes_.end(), data, data + bytes);
    if (bytes_.size() > cap_) bytes_.erase(bytes_.begin(), bytes_.end() - cap_);
}

void CappedByteFifo::Clear() {
    std::lock_guard<std::mutex> lk(mtx_);
    bytes_.clear();
}

size_t CappedByteFifo::Take(uint8_t* dst, size_t bytes) {
    size_t taken = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        taken = std::min(bytes, bytes_.size());
        std::copy(bytes_.begin(), bytes_.begin() + taken, dst);
        bytes_.erase(bytes_.begin(), bytes_.begin() + taken);
    }
    std::fill(dst + taken, dst + bytes, uint8_t{0});
    return taken;
}

}
