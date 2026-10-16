#include <cerfcore/cpu/emulated_memory.h>

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include "../boards/page_table_builder.h"
#include "../peripherals/peripheral_dispatcher.h"
#include "../platform/host_virtual_memory.h"
#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <cstring>

REGISTER_SERVICE(EmulatedMemory);

void EmulatedMemory::OnReady() {
    auto& page = emu_.Get<PageTableBuilder>();
    for (const auto& r : page.BackedMemoryRegions()) {
        AddRegion(r.pa_base, r.size, r.kind, r.decode_span);
    }
}

void EmulatedMemory::AddRegion(uint32_t base, uint32_t size,
                               MemoryKind kind, uint32_t decode_span) {
    std::lock_guard<std::mutex> lk(add_mutex_);

    const uint32_t span = decode_span ? decode_span : size;
    if (size == 0 || span < size || uint64_t(base) + span > (uint64_t{1} << 32) ||
        span % size != 0 ||
        (span != size && (size & (size - 1u)) != 0)) {
        LOG(Caution, "EmulatedMemory::AddRegion invalid region: base=0x%08X "
                "size=0x%X span=0x%X\n", base, size, span);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    if (emu_.Get<PeripheralDispatcher>().OverlapsDataInversion(base, span)) {
        emu_.Get<Fatal>().Die("EmulatedMemory::AddRegion [0x%08X..0x%llX) overlaps a bus "
                              "data inversion range", base,
                              static_cast<unsigned long long>(base) + span);
    }
    const uint32_t wrap_mask = (span == size) ? 0xFFFFFFFFu : (size - 1u);

    /* count_.load(acquire) inside the writer lock - we hold the writer
       lock, so no other writer can be racing; the acquire pairs with
       earlier store_release publications. */
    const size_t n = count_.load(std::memory_order_acquire);

    for (size_t i = 0; i < n; ++i) {
        const Region& r = regions_[i];
        if (uint64_t(base) < uint64_t(r.base) + r.span &&
            uint64_t(r.base) < uint64_t(base) + span) {
            LOG(Caution, "EmulatedMemory::AddRegion overlap: new "
                    "[0x%08X..0x%llX) vs existing [0x%08X..0x%llX)\n",
                    base, static_cast<unsigned long long>(base) + span,
                    r.base, static_cast<unsigned long long>(r.base) + r.span);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
    }

    if (n >= kMaxRegions) {
        LOG(Caution, "EmulatedMemory::AddRegion: kMaxRegions=%zu exceeded "
                "(new [0x%08X..0x%08X)). Bump kMaxRegions in "
                "emulated_memory.h.\n",
                kMaxRegions, base, base + size);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    regions_[n].base         = base;
    regions_[n].size         = size;
    regions_[n].span         = span;
    regions_[n].wrap_mask    = wrap_mask;
    regions_[n].kind         = kind;

    count_.store(n + 1, std::memory_order_release);

    LOG(Mem, "AddRegion 0x%08X size 0x%X span 0x%X kind %u (slot %zu)\n",
        base, size, span, static_cast<unsigned>(kind), n);
}

bool EmulatedMemory::OverlapsRegion(uint32_t base, uint32_t size) const {
    const size_t n = count_.load(std::memory_order_acquire);
    for (size_t i = 0; i < n; ++i) {
        const Region& r = regions_[i];
        if (uint64_t(base) < uint64_t(r.base) + r.span &&
            uint64_t(r.base) < uint64_t(base) + size) {
            return true;
        }
    }
    return false;
}

EmulatedMemory::Region* EmulatedMemory::FindRegion(uint32_t vaddr) {
    const size_t n = count_.load(std::memory_order_acquire);
    for (size_t i = 0; i < n; ++i) {
        Region& r = regions_[i];
        if (vaddr >= r.base && vaddr - r.base < r.span) {
            return &r;
        }
    }
    return nullptr;
}

uint8_t* EmulatedMemory::EnsureBacked(Region* r) {
    if (uint8_t* ptr = r->host_ptr.load(std::memory_order_acquire)) {
        return ptr;
    }

    uint8_t* allocated = host_platform::AllocateZeroed(r->size);
    if (!allocated) {
        LOG(Caution, "EmulatedMemory::EnsureBacked allocation failed "
                "region 0x%08X size 0x%X\n", r->base, r->size);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    uint8_t* expected = nullptr;
    if (r->host_ptr.compare_exchange_strong(
            expected, allocated,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        LOG(Mem, "Region 0x%08X backed at host %p\n", r->base, allocated);
        return allocated;
    }

    host_platform::Release(allocated);
    return expected;
}

void EmulatedMemory::SetAddressDecode(uint32_t region_base, const AddressDecode* decode) {
    Region* r = FindRegion(region_base);
    if (!r || r->base != region_base || r->span != r->size ||
        (r->size & (r->size - 1u)) != 0u)
        emu_.Get<Fatal>().Die("EmulatedMemory::SetAddressDecode needs a power-of-two "
                              "region at 0x%08X", region_base);
    if (decode) {
        const uint32_t backed_bits = r->size - 1u;
        bool fits = (decode->pass_mask & (PAGE_SIZE - 1u)) == PAGE_SIZE - 1u;
        for (uint32_t moved = decode->gather_mask; fits && moved != 0u; moved &= moved - 1u)
            fits = (decode->targets[std::countr_zero(moved)] & ~backed_bits) == 0u;
        if (!fits)
            emu_.Get<Fatal>().Die("EmulatedMemory::SetAddressDecode region 0x%08X size 0x%X: "
                                  "pass 0x%08X gather 0x%08X leaves the page or the region",
                                  region_base, r->size, decode->pass_mask,
                                  decode->gather_mask);
    }
    r->decode.store(decode, std::memory_order_release);
    LOG(Mem, "SetAddressDecode region 0x%08X pass 0x%08X gather 0x%08X\n", region_base,
        decode ? decode->pass_mask : 0xFFFFFFFFu, decode ? decode->gather_mask : 0u);
    for (auto& fn : decode_listeners_) fn();
}

void EmulatedMemory::RegisterDecodeListener(std::function<void()> fn) {
    decode_listeners_.push_back(std::move(fn));
}

CERF_FORCEINLINE uint8_t* EmulatedMemory::PhysicalTranslate(uint32_t pa, bool write,
                                                            cerfengine::PhysicalPage& page) {
    Region* r = FindRegion(pa);
    if (!r) return nullptr;
    const bool flash = IsFlash(*r);
    if (write && flash) return nullptr;

    uint8_t* const host    = EnsureBacked(r);
    const uint32_t page_pa = pa & ~(PAGE_SIZE - 1u);
    if (page_pa >= r->base) {
        const uint32_t offset = page_pa - r->base;
        const uint32_t backed = BackedOffset(*r, offset);
        if (RangeLinear(*r, offset, PAGE_SIZE, backed)) {
            page.read  = host + backed;
            page.write = flash ? nullptr : host + backed;
        }
    }
    return host + BackedOffset(*r, pa - r->base);
}

uint8_t* EmulatedMemory::Translate(uint32_t vaddr) {
    Region* r = FindRegion(vaddr);
    if (!r)
        emu_.Get<Fatal>().Die("EmulatedMemory::Translate unmapped 0x%08X", vaddr);
    return EnsureBacked(r) + BackedOffset(*r, vaddr - r->base);
}

uint8_t* EmulatedMemory::TryTranslate(uint32_t paddr) {
    Region* r = FindRegion(paddr);
    if (!r) return nullptr;
    return EnsureBacked(r) + BackedOffset(*r, paddr - r->base);
}

uint8_t* EmulatedMemory::TryTranslateWrite(uint32_t paddr) {
    Region* r = FindRegion(paddr);
    if (!r) {
        return nullptr;
    }

    if (IsFlash(*r)) {
        return nullptr;
    }

    return EnsureBacked(r) + BackedOffset(*r, paddr - r->base);
}

bool EmulatedMemory::IsFlash(const Region& r) {
    return r.kind != MemoryKind::kRam;
}

EmulatedMemory::Region* EmulatedMemory::BulkRegion(uint32_t vaddr, size_t size) {
    Region* r = FindRegion(vaddr);
    if (!r || static_cast<uint64_t>(vaddr) + size >
                  static_cast<uint64_t>(r->base) + r->size) {
        return nullptr;
    }
    return r;
}

bool EmulatedMemory::CanCopyRange(uint32_t paddr, size_t size, bool writable) {
    const Region* r = BulkRegion(paddr, size);
    return r && (!writable || !IsFlash(*r));
}

uint8_t* EmulatedMemory::TryTranslateRange(uint64_t paddr, uint64_t size, bool write) {
    if (size == 0 || paddr > UINT32_MAX || size > (uint64_t{1} << 32) - paddr)
        return nullptr;
    Region* r = FindRegion(static_cast<uint32_t>(paddr));
    if (!r || (write && IsFlash(*r)))
        return nullptr;
    const uint64_t offset = paddr - r->base;
    const uint64_t backed_offset = BackedOffset(*r, static_cast<uint32_t>(offset));
    if (!RangeLinear(*r, offset, size, backed_offset))
        return nullptr;
    return EnsureBacked(r) + static_cast<size_t>(backed_offset);
}

CERF_FORCEINLINE bool EmulatedMemory::PhysicalSpanUniform(uint32_t pa, uint32_t span_bytes) {
    if (span_bytes <= 0x1000u) return true;
    const uint32_t base = pa & ~(span_bytes - 1u);
    Region* r = FindRegion(base);
    if (!r) return true;
    if (r->wrap_mask != 0xFFFFFFFFu) return false;
    const uint64_t block = DecodeBlock(*r);
    const uint64_t offset = base - r->base;
    if (offset / block != (offset + span_bytes - 1u) / block) return false;
    return base >= r->base &&
           static_cast<uint64_t>(base) + span_bytes <=
               static_cast<uint64_t>(r->base) + r->size;
}

uint8_t* CERFENGINE_FASTCALL cerfengine::host::PhysicalTranslate(void* context, uint32_t pa,
                                                                 bool write,
                                                                 PhysicalPage& page) {
    return static_cast<EmulatedMemory*>(context)->PhysicalTranslate(pa, write, page);
}

bool CERFENGINE_FASTCALL cerfengine::host::PhysicalSpanUniform(void* context, uint32_t pa,
                                                               uint32_t span) {
    return static_cast<EmulatedMemory*>(context)->PhysicalSpanUniform(pa, span);
}

uint8_t EmulatedMemory::ReadByte(uint32_t vaddr) {
    return *Translate(vaddr);
}

uint16_t EmulatedMemory::ReadHalf(uint32_t vaddr) {
    return cerf::le::U16(Translate(vaddr));
}

uint32_t EmulatedMemory::ReadWord(uint32_t vaddr) {
    return cerf::le::U32(Translate(vaddr));
}

uint64_t EmulatedMemory::ReadDword(uint32_t vaddr) {
    return cerf::le::U64(Translate(vaddr));
}

void EmulatedMemory::WriteByte(uint32_t vaddr, uint8_t value) {
    *Translate(vaddr) = value;
}

void EmulatedMemory::WriteHalf(uint32_t vaddr, uint16_t value) {
    cerf::le::Put16(Translate(vaddr), value);
}

void EmulatedMemory::WriteWord(uint32_t vaddr, uint32_t value) {
    cerf::le::Put32(Translate(vaddr), value);
}

void EmulatedMemory::WriteDword(uint32_t vaddr, uint64_t value) {
    cerf::le::Put64(Translate(vaddr), value);
}

EmulatedMemory::Region* EmulatedMemory::BulkRegionFor(uint32_t vaddr,
                                                      size_t size,
                                                      const char* op) {
    if (Region* ok = BulkRegion(vaddr, size)) return ok;
    Region* r = FindRegion(vaddr);
    if (!r) {
        LOG(Caution, "EmulatedMemory::%s unmapped address "
                "0x%08X size 0x%zX\n", op, vaddr, size);
    } else {
        LOG(Caution, "EmulatedMemory::%s crosses region boundary at "
                "0x%08X size 0x%zX (region 0x%08X size 0x%X)\n",
                op, vaddr, size, r->base, r->size);
    }
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void EmulatedMemory::CopyIn(uint32_t vaddr, const void* host_src, size_t size) {
    Region* r = BulkRegionFor(vaddr, size, "CopyIn");
    uint8_t* host = EnsureBacked(r);
    const uint8_t* src = static_cast<const uint8_t*>(host_src);
    const uint64_t block = DecodeBlock(*r);
    for (size_t done = 0; done < size;) {
        const uint32_t offset = vaddr - r->base + static_cast<uint32_t>(done);
        const size_t chunk = static_cast<size_t>(
            std::min<uint64_t>(size - done, block - offset % block));
        std::memcpy(host + BackedOffset(*r, offset), src + done, chunk);
        done += chunk;
    }
}

void EmulatedMemory::CopyOut(uint32_t vaddr, void* host_dst, size_t size) {
    Region* r = BulkRegionFor(vaddr, size, "CopyOut");
    const uint8_t* host = EnsureBacked(r);
    uint8_t* dst = static_cast<uint8_t*>(host_dst);
    const uint64_t block = DecodeBlock(*r);
    for (size_t done = 0; done < size;) {
        const uint32_t offset = vaddr - r->base + static_cast<uint32_t>(done);
        const size_t chunk = static_cast<size_t>(
            std::min<uint64_t>(size - done, block - offset % block));
        std::memcpy(dst + done, host + BackedOffset(*r, offset), chunk);
        done += chunk;
    }
}

uint32_t EmulatedMemory::TryCopyOut(uint32_t paddr, uint8_t* host_dst, uint32_t size) {
    if (size == 0u) return 0u;
    if (const uint8_t* span = TryTranslateRange(paddr, size)) {
        std::memcpy(host_dst, span, size);
        return size;
    }
    uint32_t k = 0;
    for (; k < size; ++k) {
        const uint8_t* byte = TryTranslate(paddr + k);
        if (byte == nullptr) break;
        host_dst[k] = *byte;
    }
    return k;
}

uint32_t EmulatedMemory::TryCopyIn(uint32_t paddr, const uint8_t* host_src, uint32_t size) {
    if (size == 0u) return 0u;
    if (uint8_t* span = TryTranslateRange(paddr, size, true)) {
        std::memcpy(span, host_src, size);
        return size;
    }
    uint32_t k = 0;
    for (; k < size; ++k) {
        uint8_t* byte = TryTranslateWrite(paddr + k);
        if (byte == nullptr) break;
        *byte = host_src[k];
    }
    return k;
}

uint32_t EmulatedMemory::WritableLength(uint32_t paddr, uint32_t size) {
    if (size == 0u) return 0u;
    if (TryTranslateRange(paddr, size, true) != nullptr) return size;
    uint32_t k = 0;
    while (k < size && TryTranslateWrite(paddr + k) != nullptr) ++k;
    return k;
}

void EmulatedMemory::WipeVolatileRegions() {
    const size_t n = count_.load(std::memory_order_acquire);
    for (size_t i = 0; i < n; ++i) {
        Region& r = regions_[i];
        if (IsFlash(r)) {
            continue;
        }
        /* Unbacked regions are already zero on first touch. */
        if (uint8_t* host = r.host_ptr.load(std::memory_order_acquire)) {
            std::memset(host, 0, r.size);
            LOG(Mem, "WipeVolatileRegions: region 0x%08X size 0x%X zeroed\n",
                r.base, r.size);
        }
    }
}

void EmulatedMemory::SaveState(StateWriter& w) { SaveRegions(w, false); }

void EmulatedMemory::SaveFlashRegions(StateWriter& w) { SaveRegions(w, true); }

void EmulatedMemory::RestoreState(StateReader& r) { RestoreRegions(r, false); }

void EmulatedMemory::RestoreFlashRegions(StateReader& r) { RestoreRegions(r, true); }

uint32_t EmulatedMemory::RegionCount(bool flash) const {
    const size_t n = count_.load(std::memory_order_acquire);
    uint32_t count = 0;
    for (size_t i = 0; i < n; ++i) {
        if (IsFlash(regions_[i]) == flash) ++count;
    }
    return count;
}

void EmulatedMemory::SaveRegions(StateWriter& w, bool flash) {
    const size_t n = count_.load(std::memory_order_acquire);
    w.Write(flash ? "flash_count" : "volatile_count", RegionCount(flash));
    for (size_t i = 0; i < n; ++i) {
        Region& r = regions_[i];
        if (IsFlash(r) != flash) continue;
        w.Write("base", r.base);
        w.Write("region_size", r.size);
        uint8_t* host = r.host_ptr.load(std::memory_order_acquire);
        const uint8_t backed = host ? 1u : 0u;
        w.Write("backed", backed);
        if (host) w.WriteBytes("host", host, r.size);
    }
}

void EmulatedMemory::RestoreRegions(StateReader& r, bool flash) {
    const char* kind = flash ? "flash" : "RAM";
    uint32_t count = 0;
    r.Read(flash ? "flash_count" : "volatile_count", count);
    if (count != RegionCount(flash))
        r.Reject("the image has %u %s regions, this build has %u", count, kind,
                 RegionCount(flash));
    for (uint32_t k = 0; k < count; ++k) {
        uint32_t base = 0, size = 0;
        r.Read("base", base);
        r.Read("region_size", size);
        Region* reg = FindRegion(base);
        if (!reg || reg->base != base || reg->size != size || IsFlash(*reg) != flash)
            r.Reject("%s region base=0x%08X size=0x%X is not in this memory map",
                     kind, base, size);
        uint8_t backed = 0;
        r.Read("backed", backed);
        if (backed) {
            r.ReadBytes("host", EnsureBacked(reg), size);
        } else if (uint8_t* host = reg->host_ptr.load(std::memory_order_acquire)) {
            std::memset(host, 0, size);
        }
    }
}

uint64_t EmulatedMemory::VolatileByteCount() const {
    const size_t n = count_.load(std::memory_order_acquire);
    uint64_t total = 0;
    for (size_t i = 0; i < n; ++i) {
        const Region& r = regions_[i];
        if (IsFlash(r))
            continue;
        total += r.size;
    }
    return total;
}
