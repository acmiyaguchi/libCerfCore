#include <cerfcore/state/hibernation.h>

#include <cerfcore/state/emulation_freeze.h>
#include <cerfcore/state/state_image_format.h>
#include <cerfcore/state/state_stream.h>

#include <cerfcore/boot/rom_parser_service.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/host/emulation_pause.h>
#include <cerfcore/host/host_presentation.h>
#include <cerfcore/host/notification_stack.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include <cerfcore/boot/guest_cold_boot.h>
#include <cerfcore/host/host_widget_registry.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_customizations_reset.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/host/hw_screen.h>
#include "../execution/engine_bridge.h"
#include <cerfcore/peripherals/peripheral_base.h>
#include "../peripherals/peripheral_dispatcher.h"
#include <cerfcore/platform/host_file.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>

REGISTER_SERVICE(Hibernation);

Hibernation::~Hibernation() {
    JoinWorker();
}

void Hibernation::JoinWorker() {
    if (worker_.joinable()) worker_.join();
}

void Hibernation::RegisterPreSaveListener(std::function<void()> fn) {
    pre_save_listeners_.push_back(std::move(fn));
}

void Hibernation::SaveAsync(const std::string& path, std::function<void(bool)> on_done) {
    JoinWorker();
    worker_ = std::thread([this, path, on_done = std::move(on_done)] {
        const bool saved = Save(path);
        if (on_done) on_done(saved);
    });
}

void Hibernation::RestoreAsync(const std::string& path, bool ram_only) {
    JoinWorker();
    worker_ = std::thread([this, path, ram_only] { Restore(path, ram_only); });
}

void Hibernation::OnShutdown() {
    JoinWorker();
}

void Hibernation::Progress(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    emu_.Get<HwScreen>().AddLine(buf);
    LOG(Cerf, "[HIBERNATE] %s\n", buf);
}

std::string Hibernation::DeviceDirFile(const char* name) const {
    return GetDeviceDir(emu_.Get<DeviceConfig>().device_name) + name;
}

std::string Hibernation::DefaultStatePath() const {
    return DeviceDirFile(kDefaultStateFile);
}

bool Hibernation::DefaultStateExists() const {
    return host_platform::HostPathExists(DefaultStatePath());
}

uint32_t Hibernation::PeripheralLayoutSig() const {
    const auto periphs = emu_.Get<PeripheralDispatcher>().RegisteredPeripherals();
    uint32_t sig = static_cast<uint32_t>(periphs.size());
    for (const Peripheral* p : periphs)
        sig = sig * 2654435761u + p->MmioBase();
    return sig;
}

StateImageHeader Hibernation::LiveHeader() const {
    auto* rom = emu_.TryGet<RomParserService>();
    StateImageHeader h{};
    h.rom_entry_va      = (rom && rom->Ok()) ? rom->Primary().entry_va : 0;
    h.periph_layout_sig = PeripheralLayoutSig();
    uint64_t total = 0;
    if (rom) for (const auto& p : rom->Loaded()) total += p.raw.size();
    h.rom_total_bytes = total;
    h.guest_additions = emu_.Get<DeviceConfig>().guest_additions ? 1u : 0u;
    return h;
}

bool Hibernation::WriteImage(const std::string& path, std::string& error) {
    for (auto& fn : pre_save_listeners_) fn();
    StateWriter w(path);
    if (!w.Ok()) {
        error = w.Error();
        return false;
    }
    w.WriteRaw(kStateMagic, sizeof(kStateMagic));
    const StateImageHeader h = LiveHeader();
    w.Write("rom_entry_va", h.rom_entry_va);
    w.Write("periph_layout_sig", h.periph_layout_sig);
    w.Write("rom_total_bytes", h.rom_total_bytes);
    w.Write("guest_additions", h.guest_additions);

    for (const StateSection section : kStateSectionOrder) {
        w.BeginFrame(static_cast<uint32_t>(section));
        SaveSection(w, section);
        w.EndFrame();
    }
    const bool ok = w.Ok() && w.Commit();
    if (!ok) error = w.Error();
    return ok;
}

void Hibernation::SaveSection(StateWriter& w, StateSection section) {
    switch (section) {
        case StateSection::Cpu:   emu_.Get<EngineBridge>().SaveCpuState(w); break;
        case StateSection::Mmu:   emu_.Get<EngineBridge>().SaveMmuState(w); break;
        case StateSection::Ram:   emu_.Get<EmulatedMemory>().SaveState(w); break;
        case StateSection::Flash: emu_.Get<EmulatedMemory>().SaveFlashRegions(w); break;
        case StateSection::Periph: {
            const auto periphs = emu_.Get<PeripheralDispatcher>().RegisteredPeripherals();
            w.Write<uint32_t>("periph_count", static_cast<uint32_t>(periphs.size()));
            for (Peripheral* p : periphs) {
                w.BeginFrame(p->MmioBase());
                p->SaveState(w);
                w.EndFrame();
            }
            break;
        }
        case StateSection::Presentation: {
            auto& presentation = emu_.Get<HostPresentation>();
            w.Write<uint32_t>("surface_width", presentation.SurfaceWidth());
            w.Write<uint32_t>("surface_height", presentation.SurfaceHeight());
            break;
        }
        case StateSection::Widget: emu_.Get<HostWidgetRegistry>().SaveState(w); break;
        case StateSection::Reset:
            emu_.Get<GuestCpuReset>().SaveState(w);
            emu_.Get<GuestColdBoot>().SaveState(w);
            if (auto* c = emu_.TryGet<CerfVirtCustomizationsReset>())
                c->SaveState(w);
            break;
    }
}

bool Hibernation::Save(const std::string& path_in) {
    const std::string path = path_in.empty() ? DefaultStatePath() : path_in;
    auto& pause = emu_.Get<EmulationPause>();
    auto& stack = emu_.Get<NotificationStack>();

    stack.Show(NotificationId::SavingState, NotificationKind::Warning,
               "Saving the machine state…");
    Progress("Saving state...");
    Progress("Saving RAM (%llu MB)...", static_cast<unsigned long long>(
        emu_.Get<EmulatedMemory>().VolatileByteCount() >> 20));

    pause.BeginStateOperation();
    bool ok = false;
    std::string error;
    {
        auto snap = emu_.Get<EmulationFreeze>().SnapshotSection();
        ok = WriteImage(path, error);
    }
    pause.EndStateOperation(false);

    stack.Close(NotificationId::SavingState);
    if (ok) {
        Progress("State saved.");
    } else {
        Progress("Save FAILED: %s", error.c_str());
        stack.PostError("Failed to save the machine state\n" + error);
    }

    if (ok) {
        emu_.Get<HostPresentation>().SaveScreenshot(DeviceDirFile("saved_state.png"));
    }
    return ok;
}

void Hibernation::ReadHeader(StateReader& r) {
    char magic[sizeof(kStateMagic)] = {};
    r.ReadRaw(magic, sizeof(magic));
    if (std::memcmp(magic, kStateMagic, sizeof(magic)) != 0) {
        const bool cerf = std::memcmp(magic, kStateMagic, sizeof(magic) - 1) == 0;
        throw StateImageRejected(cerf ? "the image uses another CERF state format"
                                      : "not a CERF state image");
    }
    StateImageHeader saved{};
    r.Read("rom_entry_va", saved.rom_entry_va);
    r.Read("periph_layout_sig", saved.periph_layout_sig);
    r.Read("rom_total_bytes", saved.rom_total_bytes);
    r.Read("guest_additions", saved.guest_additions);
    const StateImageHeader live = LiveHeader();
    if (saved.rom_entry_va != live.rom_entry_va || saved.rom_total_bytes != live.rom_total_bytes)
        throw StateImageRejected("the image is for a different ROM");
    if (saved.periph_layout_sig != live.periph_layout_sig)
        throw StateImageRejected("the image has a different peripheral set");
    if (saved.guest_additions != live.guest_additions)
        throw StateImageRejected(saved.guest_additions
                                     ? "the image was saved with guest additions"
                                     : "the image was saved without guest additions");
}

void Hibernation::RestorePeripherals(StateReader& r) {
    const auto periphs = emu_.Get<PeripheralDispatcher>().RegisteredPeripherals();
    uint32_t n = 0;
    r.Read("periph_count", n);
    if (n != periphs.size())
        r.Reject("the image has %u peripherals, this build has %zu", n, periphs.size());
    for (Peripheral* p : periphs) {
        const uint32_t base = r.EnterFrame();
        if (base != p->MmioBase())
            r.Reject("the image has peripheral 0x%08X where this build has 0x%08X",
                     base, p->MmioBase());
        p->RestoreState(r);
        r.LeaveFrame();
    }
    for (Peripheral* p : periphs) p->PostRestore();
}

void Hibernation::RestorePresentation(StateReader& r) {
    uint32_t w = 0, h = 0;
    r.Read("surface_width", w);
    r.Read("surface_height", h);
    if (w == 0 || h == 0) return;
    emu_.Get<HostPresentation>().RestoreSurfaceSize(w, h);
}

void Hibernation::ApplyImage(StateReader& r, bool ram_only) {
    for (const StateSection section : kStateSectionOrder) {
        if (r.Remaining() == 0)
            r.Reject("section %u is missing from the image", static_cast<uint32_t>(section));
        const uint32_t id = r.EnterFrame();
        if (id != static_cast<uint32_t>(section))
            r.Reject("section %u where this build reads section %u", id,
                     static_cast<uint32_t>(section));
        if (ram_only && section != StateSection::Ram && section != StateSection::Flash) {
            r.SkipFrame();
            continue;
        }
        RestoreSection(r, section);
        r.LeaveFrame();
    }
    if (r.Remaining() != 0)
        r.Reject("the image carries %llu bytes past its last section",
                 static_cast<unsigned long long>(r.Remaining()));
}

void Hibernation::RestoreSection(StateReader& r, StateSection section) {
    switch (section) {
        case StateSection::Cpu:    emu_.Get<EngineBridge>().RestoreCpuState(r); break;
        case StateSection::Mmu:    emu_.Get<EngineBridge>().RestoreMmuState(r); break;
        case StateSection::Ram:    emu_.Get<EmulatedMemory>().RestoreState(r); break;
        case StateSection::Flash:  emu_.Get<EmulatedMemory>().RestoreFlashRegions(r); break;
        case StateSection::Periph: RestorePeripherals(r); break;
        case StateSection::Presentation: RestorePresentation(r); break;
        case StateSection::Widget: emu_.Get<HostWidgetRegistry>().RestoreState(r); break;
        case StateSection::Reset:
            emu_.Get<GuestCpuReset>().RestoreState(r);
            emu_.Get<GuestColdBoot>().RestoreState(r);
            if (auto* c = emu_.TryGet<CerfVirtCustomizationsReset>())
                c->RestoreState(r);
            break;
    }
}

void Hibernation::RollBack(const std::string& rollback_path) {
    StateReader r(rollback_path);
    try {
        if (!r.Ok()) throw StateImageRejected("the rollback image cannot be opened: " + r.Error());
        ReadHeader(r);
        ApplyImage(r, false);
    } catch (const StateImageRejected& e) {
        emu_.Get<Fatal>().Die("Hibernation: the rollback image this build just wrote "
                              "was refused: %s", e.what());
    }
}

bool Hibernation::Restore(const std::string& path_in, bool ram_only) {
    const std::string path = path_in.empty() ? DefaultStatePath() : path_in;
    auto& stack = emu_.Get<NotificationStack>();

    stack.Show(NotificationId::LoadingState, NotificationKind::Warning,
               "Loading the machine state…");
    Progress(ram_only ? "Warm boot: restoring RAM..." : "Restoring state...");

    bool ok = false;
    std::string reason;
    StateReader r(path);
    if (r.Ok()) {
        ok = ApplyStateFile(r, ram_only, reason);
    } else {
        reason = r.Error();
        Progress("Cannot open state image: %s", reason.c_str());
    }

    stack.Close(NotificationId::LoadingState);
    if (!ok)
        stack.PostError("Failed to load the save state\n" + reason);
    return ok;
}

bool Hibernation::ApplyStateFile(StateReader& r, bool ram_only, std::string& reason) {
    auto& pause = emu_.Get<EmulationPause>();
    pause.BeginStateOperation();
    auto snap = emu_.Get<EmulationFreeze>().SnapshotSection();
    bool ok = false;
    try {
        ReadHeader(r);
        const std::string rollback = DeviceDirFile(kRollbackStateFile);
        std::string write_error;
        if (!WriteImage(rollback, write_error))
            throw StateImageRejected("the rollback image cannot be written: " + write_error);
        uint32_t ignored = 0;
        try {
            ApplyImage(r, ram_only);
            ok = true;
        } catch (const StateImageRejected&) {
            RollBack(rollback);
            host_platform::DeleteHostFile(rollback, ignored);
            throw;
        }
        host_platform::DeleteHostFile(rollback, ignored);
        emu_.Get<EngineBridge>().Guest().FlushTranslationCache();
    } catch (const StateImageRejected& e) {
        reason = e.what();
        emu_.Get<EngineBridge>().Guest().FlushTranslationCache();
    }
    snap.unlock();

    if (ok) {
        Progress("State restored.");
        if (!ram_only) emu_.Get<GuestDeepSleep>().OnFullRestore();
    } else {
        Progress("Restore refused: %s", reason.c_str());
    }
    pause.EndStateOperation(ok);
    return ok;
}
