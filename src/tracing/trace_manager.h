#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace cerfengine { struct MipsCpuState; }

namespace cerfcore {

class CerfEmulator;

/* TraceContext - read-only snapshot the JIT hands to a trace handler. Only the
   active ISA's view is non-null: ARM populates `regs`(`regs[15]`==pc)+`cpsr`
   (mips==null); MIPS populates `mips` (regs==null). A handler dereferencing the
   other ISA's view reads null. `pc`/`ReadVa*` are ISA-neutral. */
struct TraceContext {
    const uint32_t* regs;
    uint32_t        cpsr;
    uint32_t        pc;
    CerfEmulator&   emu;
    const cerfengine::MipsCpuState* mips = nullptr;

    std::optional<uint8_t>  ReadVa8 (uint32_t va) const;
    std::optional<uint16_t> ReadVa16(uint32_t va) const;
    std::optional<uint32_t> ReadVa32(uint32_t va) const;
};

using TraceHandler   = std::function<void(const TraceContext&)>;
using TracePredicate = std::function<bool(const TraceContext&)>;

class TraceManager : public Service {
public:
    using Service::Service;
    void OnReady() override;

    /* Runs `register_fn` (which calls OnPc) iff `expected_crc32` matches the
       computed bundle CRC32; otherwise discards it. */
    void RegisterForBundle(uint32_t expected_crc32,
                           const std::function<void()>& register_fn);

    void OnPc(uint32_t runtime_va, TraceHandler handler);
    void OnPcFiltered(uint32_t       runtime_va,
                      TracePredicate predicate,
                      TraceHandler   handler);

    /* Hot-path predicate. Single map lookup; empty map = single
       branch on size(). */
    bool HasPcTrace (uint32_t pc) const;
    bool HasPcTraceIn(uint32_t first_pc, uint32_t last_pc) const;

    /* Dispatch sites called from the JIT translator (one per live ISA). */
    void DispatchPc(uint32_t pc, const uint32_t* regs, uint32_t cpsr);
    void DispatchPcMips(uint32_t pc, const cerfengine::MipsCpuState* st);

    uint32_t BundleCrc32() const { return bundle_crc32_; }

#if CERF_DEV_MODE
    void OnRunLoopIter(TraceHandler handler);
    void DispatchRunLoopIter(const uint32_t* regs, uint32_t cpsr);
    void DispatchRunLoopIterMips(const cerfengine::MipsCpuState* st);
#endif

private:
    /* Computes CRC32 over concatenated RomParserService::Loaded()[i].raw
       bytes in load order. Empty when no ROMs loaded. */
    uint32_t ComputeBundleCrc32() const;

    /* Shared PC-trace fan-out for both ISAs: lookup pc, run predicate+handler
       over the prebuilt context. */
    void DispatchContext(uint32_t pc, const TraceContext& ctx);

    /* Halts if a hook is already bound at runtime_va - a VA may be hooked
       exactly once, via OnPc or OnPcFiltered. */
    void GuardUnique(uint32_t runtime_va);
    void NoteTracedPc(uint32_t runtime_va);

    uint32_t bundle_crc32_   = 0;
    uint32_t bundles_matched_ = 0;
    uint32_t bundles_skipped_ = 0;

    struct PcEntry {
        std::optional<TracePredicate> predicate;
        TraceHandler                  handler;
    };
    std::unordered_map<uint32_t, PcEntry> pc_traces_;
    std::vector<uint32_t>                 traced_pcs_;

#if CERF_DEV_MODE
    void DispatchIterContext(const TraceContext& ctx);
    std::vector<TraceHandler> iter_handlers_;
#endif
};

}
