#include <cerfcore/host/refresh_rate_service.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/log.h>
#include <cerfcore/host/host_displays.h>

namespace cerfcore {

REGISTER_SERVICE(RefreshRateService);

namespace {

int SnapUpToNormal(int hz) {
    static const int kNormal[] = { 60, 75, 90, 100, 120, 144, 165, 180,
                                   200, 240, 300, 360, 480 };
    for (int n : kNormal)
        if (hz <= n) return n;
    return hz;
}

}

int RefreshRateService::GetRefreshRate() {
    if (cached_) return cached_;
    cached_ = Decide();
    return cached_;
}

int RefreshRateService::FrameIntervalMs() {
    const int interval = 1000 / GetRefreshRate();
    return interval < 1 ? 1 : interval;
}

int RefreshRateService::Decide() {
    auto& dc = emu_.Get<DeviceConfig>();

    if (dc.screen_refresh_rate != 0) {
        LOG(Boot, "RefreshRate: --screen-refresh-rate=%u Hz\n",
            dc.screen_refresh_rate);
        return (int)dc.screen_refresh_rate;
    }

    int raw = emu_.Get<HostDisplays>().HighestRefreshRate();
    if (raw < 1) raw = 60;

    if (dc.guest_additions) {
        int snapped = SnapUpToNormal(raw);
        LOG(Boot, "RefreshRate: guest additions host max=%d Hz -> %d Hz\n",
            raw, snapped);
        return snapped;
    }

    int capped = (raw < 60) ? raw : 60;
    LOG(Boot, "RefreshRate: non-GA host max=%d Hz -> %d Hz\n", raw, capped);
    return capped;
}

}
