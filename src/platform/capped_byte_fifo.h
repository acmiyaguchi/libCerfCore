#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

namespace cerfcore {

class CappedByteFifo {
public:
    void   SetCap(size_t cap_bytes);
    void   Push(const uint8_t* data, size_t bytes);
    void   Clear();
    size_t Take(uint8_t* dst, size_t bytes);

private:
    std::mutex          mtx_;
    std::deque<uint8_t> bytes_;
    size_t              cap_ = 0;
};

}
