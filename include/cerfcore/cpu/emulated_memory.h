#pragma once

#include "../core/force_inline.h"
#include "../core/service.h"
#include "memory_kind.h"
#include <cerfengine/physical_memory_host.h>

#include <atomic>
#include <bit>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace cerfcore {

class StateWriter;
class StateReader;

class EmulatedMemory : public Service {
public:
    using Service::Service;
    ~EmulatedMemory() override;

    void OnReady() override;

    static constexpr uint32_t PAGE_SIZE = 0x1000;

    static constexpr size_t kMaxRegions = 64;

    /* `decode_span` (0 = `size`) is the address space the chip select decodes onto
       this region. A span wider than `size` repeats the region through the window,
       which requires a power-of-two `size` dividing the span. */
    void AddRegion(uint32_t base, uint32_t size,
                   MemoryKind kind = MemoryKind::kRam,
                   uint32_t decode_span = 0);

    bool OverlapsRegion(uint32_t base, uint32_t size) const;

    struct AddressDecode {
        uint32_t pass_mask   = 0xFFFFFFFFu;
        uint32_t gather_mask = 0;
        uint32_t targets[32] = {};
    };

    void SetAddressDecode(uint32_t region_base, const AddressDecode* decode);
    void RegisterDecodeListener(std::function<void()> fn);

    uint8_t* Translate(uint32_t vaddr);

    uint8_t* TryTranslate(uint32_t paddr);

    uint8_t* TryTranslateWrite(uint32_t paddr);

    uint8_t* TryTranslateRange(uint64_t paddr, uint64_t size, bool write = false);

    uint8_t  ReadByte(uint32_t vaddr);
    uint16_t ReadHalf(uint32_t vaddr);
    uint32_t ReadWord(uint32_t vaddr);
    uint64_t ReadDword(uint32_t vaddr);
    void     WriteByte(uint32_t vaddr, uint8_t  value);
    void     WriteHalf(uint32_t vaddr, uint16_t value);
    void     WriteWord(uint32_t vaddr, uint32_t value);
    void     WriteDword(uint32_t vaddr, uint64_t value);

    /* Bulk copy from host buffer into emulated memory at vaddr. The
       entire range must fall inside one declared region; halts on
       unmapped destination or boundary-crossing copy. */
    void CopyIn(uint32_t vaddr, const void* host_src, size_t size);

    /* Bulk copy out of emulated memory. Same one-region contract as
       CopyIn. */
    void CopyOut(uint32_t vaddr, void* host_dst, size_t size);

    uint32_t TryCopyOut(uint32_t paddr, uint8_t* host_dst, uint32_t size);
    uint32_t TryCopyIn(uint32_t paddr, const uint8_t* host_src, uint32_t size);
    uint32_t WritableLength(uint32_t paddr, uint32_t size);

    bool CanCopyRange(uint32_t paddr, size_t size, bool writable);

    void WipeVolatileRegions();

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

    void SaveFlashRegions(StateWriter& w);
    void RestoreFlashRegions(StateReader& r);

    /* Total bytes across volatile regions - the save-progress denominator. */
    uint64_t VolatileByteCount() const;

private:
    friend uint8_t* CERFENGINE_FASTCALL cerfengine::host::PhysicalTranslate(
        void* context, uint32_t pa, bool write, cerfengine::PhysicalPage& page);
    friend bool CERFENGINE_FASTCALL cerfengine::host::PhysicalSpanUniform(
        void* context, uint32_t pa, uint32_t span);

    CERF_FORCEINLINE uint8_t* PhysicalTranslate(uint32_t pa, bool write,
                                                cerfengine::PhysicalPage& page);
    CERF_FORCEINLINE bool     PhysicalSpanUniform(uint32_t pa, uint32_t span_bytes);

    struct Region {
        uint32_t              base         = 0;
        uint32_t              size         = 0;   /* backed bytes */
        uint32_t              span         = 0;
        uint32_t              wrap_mask    = 0xFFFFFFFFu;
        std::atomic<const AddressDecode*> decode{nullptr};
        MemoryKind            kind         = MemoryKind::kRam;
        std::atomic<uint8_t*> host_ptr{nullptr};  /* lazy first-touch */
    };

    static uint32_t BackedOffset(const Region& r, uint32_t offset) {
        const uint32_t wrapped = offset & r.wrap_mask;
        const AddressDecode* d = r.decode.load(std::memory_order_acquire);
        if (!d) return wrapped;
        uint32_t backed = wrapped & d->pass_mask;
        for (uint32_t moved = wrapped & d->gather_mask; moved != 0u; moved &= moved - 1u)
            backed |= d->targets[std::countr_zero(moved)];
        return backed;
    }
    static uint64_t DecodeBlock(const Region& r) {
        const AddressDecode* d = r.decode.load(std::memory_order_acquire);
        const uint32_t moved = d ? (~d->pass_mask & (r.size - 1u)) : 0u;
        return moved ? (moved & (0u - moved)) : (uint64_t{1} << 32);
    }
    static bool RangeLinear(const Region& r, uint64_t offset, uint64_t size,
                            uint64_t backed_offset) {
        if (size > r.span - offset || size > r.size - backed_offset) return false;
        const uint64_t block = DecodeBlock(r);
        return offset / block == (offset + size - 1u) / block;
    }

    /* Lock-free read: load_acquire count_, linear scan regions_[0..n). */
    Region*  FindRegion(uint32_t vaddr);
    /* Shared CopyIn/CopyOut gate: region containing [vaddr, vaddr+size)
       or fatal (unmapped / boundary-crossing). */
    Region*  BulkRegionFor(uint32_t vaddr, size_t size, const char* op);
    Region*  BulkRegion(uint32_t vaddr, size_t size);
    static bool IsFlash(const Region& r);
    uint32_t RegionCount(bool flash) const;
    void     SaveRegions(StateWriter& w, bool flash);
    void     RestoreRegions(StateReader& r, bool flash);
    uint8_t* EnsureBacked(Region* r);

    Region              regions_[kMaxRegions]{};
    std::atomic<size_t> count_{0};
    std::mutex          add_mutex_;
    std::vector<std::function<void()>> decode_listeners_;
};

}
