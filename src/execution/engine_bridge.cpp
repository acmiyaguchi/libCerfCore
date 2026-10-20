#include "engine_bridge.h"

#include <string>

#include "../boards/page_table_builder.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include <cerfcore/host/guest_power_notifier.h>
#include "../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../tracing/trace_manager.h"
#include <cerfengine/physical_map.h>

#if CERF_DEV_MODE
#include "../core/rate_probe.h"
#endif

namespace cerfcore {

void EngineBridge::OnReady() {
    pages_ = &emu_.Get<PageTableBuilder>();
    trace_ = &emu_.Get<TraceManager>();
#if CERF_DEV_MODE
    probe_ = &emu_.Get<RateProbe>();
#endif
    EmulatedMemory& memory = emu_.Get<EmulatedMemory>();

    cerfengine::Setup setup;
    setup.host                  = this;
    setup.memory                = &memory;
    setup.mmio                  = emu_.Get<PeripheralDispatcher>().EngineBinding();
    setup.report_block_compiles = CERF_DEV_MODE != 0;
    FillSetup(setup);

    engine_ = std::make_unique<cerfengine::Engine>(setup);
    engine_->SetLogMask((Log::IsEnabled(Log::Cat::Jit) ? 1u : 0u) |
                        (1u << static_cast<uint32_t>(cerfengine::LogChannel::kCaution)));
    std::string error;
    if (!engine_->Start(error)) {
        emu_.Get<Fatal>().Die("EngineBridge: the engine refused its setup: %s", error.c_str());
    }
    guest_ = &engine_->Get<cerfengine::GuestEngine>();
    cerfengine::PhysicalMap& map = engine_->Get<cerfengine::PhysicalMap>();
    memory.RegisterDecodeListener([&map] { map.Invalidate(); });
    ColdBoot();
}

std::vector<cerfengine::DramRegion> EngineBridge::CachedDramRegions() {
    std::vector<cerfengine::DramRegion> out;
    for (const DramRegion& r : pages_->CachedDramRegions()) {
        out.push_back(cerfengine::DramRegion{r.pa_base, r.size});
    }
    return out;
}

void EngineBridge::OnResetRequested(bool is_resume) {
    emu_.Get<GuestCpuReset>().SetPendingResume(is_resume);
}

void EngineBridge::OnRebootRequested() {
    emu_.Get<GuestDeepSleep>().ClearWakeCause();
    emu_.Get<GuestPowerNotifier>().NotifyReboot();
}

void EngineBridge::OnResetDelivered() {
    emu_.Get<GuestCpuReset>().OnResetDelivered();
}

void EngineBridge::OnPowerDownRequest() {
    emu_.Get<GuestDeepSleep>().Enter();
}

void EngineBridge::OnBlockCompiled(const cerfengine::BlockCompileStats& stats) {
#if CERF_DEV_MODE
    probe_->Inc(RateProbe::Counter::JitCompiles);
    probe_->Add(RateProbe::Counter::JitGuestInsns, stats.guest_insns);
    probe_->Add(RateProbe::Counter::JitHostBytes, stats.host_bytes);
    probe_->Add(RateProbe::Counter::JitCompileNs, static_cast<uint32_t>(stats.compile_ns));
#else
    (void)stats;
#endif
}

bool EngineBridge::HasPcTrace(uint32_t pc) {
    return trace_->HasPcTrace(pc);
}

bool EngineBridge::HasPcTraceIn(uint32_t first_pc, uint32_t last_pc) {
    return trace_->HasPcTraceIn(first_pc, last_pc);
}

void EngineBridge::OnArmPcTrace(uint32_t pc, const uint32_t* gprs, uint32_t cpsr) {
    trace_->DispatchPc(pc, gprs, cpsr);
}

void EngineBridge::OnMipsPcTrace(uint32_t pc, const cerfengine::MipsCpuState* state) {
    trace_->DispatchPcMips(pc, state);
}

void EngineBridge::Panic(const char* message) {
    emu_.Get<Fatal>().Die("%s", message);
}

void EngineBridge::Log(cerfengine::LogChannel channel, const char* text) {
    if (channel == cerfengine::LogChannel::kCaution) {
        LOG(Caution, "%s", text);
    } else {
        LOG(Jit, "%s", text);
    }
}

bool EngineBridge::Symbolize(const void* address, char* out, size_t out_size) {
    return Log::SymbolizeAddress(address, out, out_size);
}

}
