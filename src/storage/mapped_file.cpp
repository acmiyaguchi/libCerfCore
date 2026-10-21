#include "mapped_file.h"

#include <algorithm>
#include <cstring>

namespace cerfcore {

MappedFile::~MappedFile() {
    if (view_) host_platform::UnmapFileView(view_, view_len_);
    host_platform::CloseMappedFile(mapping_);
}

bool MappedFile::Open(const std::string& path) {
    const bool opened = host_platform::OpenMappedFile(path, mapping_);
    granularity_ = mapping_.granularity;
    if (granularity_ == 0) granularity_ = 0x10000u;
    if (window_ < granularity_) window_ = granularity_;
    size_ = mapping_.size;
    return opened;
}

bool MappedFile::EnsureWindow(uint64_t offset, size_t len) {
    if (offset >= size_ || len > size_ - offset) return false;
    if (view_ && offset >= view_base_ &&
        offset + len <= view_base_ + view_len_) {
        return true;                                       /* already covered */
    }

    /* View offset must be a multiple of the allocation granularity. Cover from
       the aligned base through offset+len, at least one window. */
    const uint64_t base = (offset / granularity_) * granularity_;
    const size_t   need = static_cast<size_t>(offset - base) + len;
    size_t want = std::max(window_, need);
    if (base + want > size_) want = static_cast<size_t>(size_ - base);

    if (view_) { host_platform::UnmapFileView(view_, view_len_); view_ = nullptr; view_len_ = 0; }
    uint8_t* p = host_platform::MapFileView(mapping_, base, want);
    if (!p) return false;
    view_      = p;
    view_base_ = base;
    view_len_  = want;
    return true;
}

const uint8_t* MappedFile::View(uint64_t offset, size_t len) {
    if (len > window_) return nullptr;                     /* exceeds a window */
    if (!EnsureWindow(offset, len)) return nullptr;
    return view_ + static_cast<size_t>(offset - view_base_);
}

size_t MappedFile::Read(uint64_t offset, void* dst, size_t len) {
    if (offset >= size_) return 0;
    len = static_cast<size_t>(std::min<uint64_t>(len, size_ - offset));

    auto*  out    = static_cast<uint8_t*>(dst);
    size_t copied = 0;
    while (copied < len) {
        const uint64_t pos   = offset + copied;
        const size_t   chunk = std::min(len - copied, window_);
        if (!EnsureWindow(pos, chunk)) break;
        const size_t avail = view_len_ - static_cast<size_t>(pos - view_base_);
        const size_t n     = std::min(chunk, avail);
        if (n == 0) break;
        std::memcpy(out + copied, view_ + static_cast<size_t>(pos - view_base_), n);
        copied += n;
    }
    return copied;
}

}
