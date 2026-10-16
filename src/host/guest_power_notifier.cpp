#include <cerfcore/host/guest_power_notifier.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/host/frame_renderer.h>
#include <cerfcore/host/hw_screen.h>

namespace cerfcore {

REGISTER_SERVICE(GuestPowerNotifier);

void GuestPowerNotifier::Banner(const char* line) {
    LOG(Caution, "%s\n", line);
    auto& uart = emu_.Get<HwScreen>();
    uart.AddLine("");
    uart.AddLine(line);
}

void GuestPowerNotifier::RegisterRebootListener(std::function<void()> fn) {
    reboot_listeners_.push_back(std::move(fn));
}

void GuestPowerNotifier::NotifyPowerDown() {
    Banner("!! CERF: CPU Sleep !!");
}

void GuestPowerNotifier::NotifyReboot() {
    Banner("!! CERF: CPU reset !!");
    if (auto* fr = emu_.TryGet<FrameRenderer>()) fr->RearmContentLatch();
    for (auto& fn : reboot_listeners_) fn();
}

void GuestPowerNotifier::NotifyResume(ResumeSource src) {
    Banner(src == ResumeSource::Hardware ? "!! CERF: CPU Resume by hardware !!"
                                         : "!! CERF: CPU Resume by user !!");
}

void GuestPowerNotifier::NotifyHardReset() {
    Banner("!! CERF: CPU+RAM hard reset !!");
}

}
