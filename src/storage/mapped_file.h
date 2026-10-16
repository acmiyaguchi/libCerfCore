#pragma once

#include "../platform/host_file_mapping.h"

#include <cstddef>
#include <cstdint>
#include <string>

/* Read-only access to a file of any size from a 32-bit process. A whole-file
   MapViewOfFile of a multi-GiB file cannot fit the 2 GB user address space, so
   only a bounded, granularity-aligned view is mapped at a time and slid on
   demand; the file-mapping section itself costs no address space. */
class MappedFile {
public:
    static constexpr size_t kDefaultWindow = 16u * 1024 * 1024;  /* 16 MB view */

    explicit MappedFile(size_t window_bytes = kDefaultWindow)
        : window_(window_bytes) {}
    ~MappedFile();
    MappedFile(const MappedFile&)            = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    bool     Open(const std::string& path);
    bool     IsOpen() const { return mapping_.section != nullptr; }
    uint64_t Size()   const { return size_; }

    /* Zero-copy pointer to `len` contiguous bytes at `offset`. Valid only until
       the next View()/Read() - the underlying view may remap. Returns nullptr
       if the range is out of file bounds or larger than the window (use Read
       for ranges that may exceed one window). */
    const uint8_t* View(uint64_t offset, size_t len);

    /* Copies `len` bytes at `offset` into `dst`, sliding the view as needed, so
       it handles ranges far larger than the window. Returns bytes actually
       copied (< len only when the range runs past end-of-file). */
    size_t Read(uint64_t offset, void* dst, size_t len);

private:
    /* Ensure the current view covers [offset, offset+len); remap if not. */
    bool EnsureWindow(uint64_t offset, size_t len);

    host_platform::FileMapping mapping_{};
    uint8_t* view_        = nullptr;  /* base of current view (at view_base_)   */
    uint64_t view_base_   = 0;        /* file offset of view_[0], gran-aligned   */
    size_t   view_len_    = 0;        /* bytes mapped in the current view        */
    uint64_t size_        = 0;        /* total file size                         */
    uint32_t granularity_ = 0;        /* MapViewOfFile offset alignment          */
    size_t   window_      = kDefaultWindow;
};
