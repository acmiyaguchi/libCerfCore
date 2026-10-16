#pragma once

#include <cerfcore/core/force_inline.h>
#include <cerfcore/core/service.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include <cerfengine/mmio_bus_host.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace cerfcore {

using MmioWidth = cerfengine::MmioWidth;

class PeripheralDispatcher : public Service {
public:
    using Service::Service;

    using DataInversionId = uint32_t;

    void Register(Peripheral* p);

    DataInversionId InstallDataInversion(uint32_t base, uint32_t end);

    void SetDataInversion(DataInversionId id, bool inverting);

    bool OverlapsDataInversion(uint32_t base, uint32_t size) const;

    bool IsPeripheralAddress(uint32_t addr) const;

    void ValidatePhysReachable(uint32_t phys_addr_mask) const;

    std::vector<Peripheral*> RegisteredPeripherals() const;

    uint8_t  ReadByte (uint32_t addr);
    uint16_t ReadHalf (uint32_t addr);
    uint32_t ReadWord (uint32_t addr);
    void     WriteByte (uint32_t addr, uint8_t  value);
    void     WriteHalf (uint32_t addr, uint16_t value);
    void     WriteWord (uint32_t addr, uint32_t value);

    cerfengine::MmioBusBinding EngineBinding();

private:
    struct InversionRange {
        uint32_t base;
        uint32_t end;
    };

    struct InvertedTarget {
        Peripheral::FastReadFn  read;
        Peripheral::FastWriteFn write;
        void*                   ctx;
    };

    struct Entry : cerfengine::MmioRoute {
        Peripheral*           p;
        const InvertedTarget* inverted;
    };

    using EntryTable = std::vector<Entry>;

    static uint32_t InvertedRead (void* ctx, uint32_t off, uint32_t width_bytes);
    static void     InvertedWrite(void* ctx, uint32_t off, uint32_t value, uint32_t width_bytes);

    static constexpr uint32_t kMaxDataInversions = 32u;

    bool OverlapsDataInversionLocked(uint32_t base, uint64_t end) const;
    const InversionRange* InversionRangeOf(const Entry& entry) const;
    void PublishLocked();

public:
    uint32_t Read(uint32_t addr, MmioWidth width) {
        if (const Entry* e = MemoHit(addr)) return e->Read(addr, width);
        return ReadSlow(addr, width);
    }

    void Write(uint32_t addr, uint32_t value, MmioWidth width) {
        if (const Entry* e = MemoHit(addr)) {
            e->Write(addr, value, width);
            return;
        }
        WriteSlow(addr, value, width);
    }

private:
    const Entry* MemoHit(uint32_t addr) const {
        const EntryTable* t = live_.load(std::memory_order_acquire);
        if (!t) return nullptr;
        const size_t cached = last_hit_.load(std::memory_order_relaxed);
        if (cached >= t->size()) return nullptr;
        const Entry& hit = (*t)[cached];
        if (addr < hit.base || addr > hit.last) return nullptr;
        return &hit;
    }

    [[noreturn]] static void HaltBadWidth(uint32_t width);

    friend bool CERFENGINE_FASTCALL cerfengine::host::MmioFindRoute(
        void* context, uint32_t pa, cerfengine::MmioRoute& route);
    friend uint64_t CERFENGINE_FASTCALL cerfengine::host::MmioReadDword(void* context,
                                                                        uint32_t pa);
    friend void CERFENGINE_FASTCALL cerfengine::host::MmioWriteDword(void* context, uint32_t pa,
                                                                     uint64_t value);
    friend uint32_t CERFENGINE_FASTCALL cerfengine::host::MmioReadSlow(
        void* context, uint32_t pa, cerfengine::MmioWidth width, cerfengine::MmioRoute& route);
    friend void CERFENGINE_FASTCALL cerfengine::host::MmioWriteSlow(
        void* context, uint32_t pa, uint32_t value, cerfengine::MmioWidth width,
        cerfengine::MmioRoute& route);

    CERF_FORCEINLINE bool     FindRoute(uint32_t addr, cerfengine::MmioRoute& route) const;
    CERF_FORCEINLINE uint64_t ReadDwordRouted(uint32_t addr);
    CERF_FORCEINLINE void     WriteDwordRouted(uint32_t addr, uint64_t value);

    template <bool kFillRoute, MmioWidth kWidth>
    CERF_FORCEINLINE uint32_t ReadSlowAt(uint32_t addr, cerfengine::MmioRoute* route);
    template <bool kFillRoute, MmioWidth kWidth>
    CERF_FORCEINLINE void WriteSlowAt(uint32_t addr, uint32_t value,
                                      cerfengine::MmioRoute* route);
    template <bool kFillRoute>
    CERF_FORCEINLINE uint32_t ReadSlowImpl(uint32_t addr, MmioWidth width,
                                           cerfengine::MmioRoute* route);
    template <bool kFillRoute>
    CERF_FORCEINLINE void WriteSlowImpl(uint32_t addr, uint32_t value, MmioWidth width,
                                        cerfengine::MmioRoute* route);
    uint32_t ReadSlow(uint32_t addr, MmioWidth width);
    void     WriteSlow(uint32_t addr, uint32_t value, MmioWidth width);
    const Entry* LookupSlow(uint32_t addr) const;

    std::atomic<const EntryTable*>          live_{nullptr};
    std::atomic<uint32_t>                   generation_{0};
    std::vector<std::unique_ptr<EntryTable>> tables_;

    mutable std::mutex                            table_mutex_;
    EntryTable                                    entries_;
    std::vector<InversionRange>                   inversions_;
    uint32_t                                      active_inversions_ = 0u;
    std::unordered_map<uint32_t, const EntryTable*> table_by_active_;
    std::vector<std::unique_ptr<InvertedTarget>>  inverted_targets_;

    mutable std::atomic<size_t> last_hit_{0};

    const Entry* LookupEntry(uint32_t addr) const;
};

}
