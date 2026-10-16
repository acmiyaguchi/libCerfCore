#include <cerfcore/boot/guest_cold_boot.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/host/guest_power_notifier.h>
#include "../execution/engine_bridge.h"
#include <cerfcore/socs/guest_cpu_reset.h>

namespace cerfcore {

REGISTER_SERVICE(GuestColdBoot);

void GuestColdBoot::RegisterReplay(std::function<void()> fn) {
    replays_.push_back(std::move(fn));
}

void GuestColdBoot::RecordPatch(uint32_t pa, uint32_t len) {
    std::vector<uint8_t> bytes(len);
    emu_.Get<EmulatedMemory>().CopyOut(pa, bytes.data(), len);
    LOG(Boot, "GuestColdBoot: recorded %u-byte patch at PA 0x%08X\n", len, pa);
    replays_.push_back([this, pa, bytes = std::move(bytes)] {
        emu_.Get<EmulatedMemory>().CopyIn(pa, bytes.data(), bytes.size());
    });
}

void GuestColdBoot::RequestHardReset() {
    pending_.store(true, std::memory_order_release);
    emu_.Get<GuestCpuReset>().ColdReset();
}

void GuestColdBoot::SaveState(StateWriter& w) const {
    w.Write<uint8_t>("pending", pending_.load(std::memory_order_acquire) ? 1u : 0u);
}

void GuestColdBoot::RestoreState(StateReader& r) {
    uint8_t pending = 0;
    r.Read("pending", pending);
    pending_.store(pending != 0, std::memory_order_release);
}

void GuestColdBoot::ExecuteIfPending() {
    if (!pending_.exchange(false, std::memory_order_acq_rel)) return;

    LOG(Boot, "GuestColdBoot: hard reset - wiping volatile RAM, replaying "
              "%zu boot-time writes\n", replays_.size());
    emu_.Get<EmulatedMemory>().WipeVolatileRegions();
    for (auto& fn : replays_) fn();
    emu_.Get<EngineBridge>().Guest().FlushTranslationCache();
    emu_.Get<GuestPowerNotifier>().NotifyHardReset();
}

}
