#include "host_display.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace cerfcore {

namespace host_platform {

namespace {

struct MaxMonitorDims { uint32_t w = 0; uint32_t h = 0; };

BOOL CALLBACK AccumulateMaxMonitor(HMONITOR, HDC, LPRECT rc, LPARAM lp) {
    auto* m = reinterpret_cast<MaxMonitorDims*>(lp);
    const uint32_t w = (uint32_t)(rc->right - rc->left);
    const uint32_t h = (uint32_t)(rc->bottom - rc->top);
    if ((uint64_t)w * h > (uint64_t)m->w * m->h) { m->w = w; m->h = h; }
    return TRUE;
}

BOOL CALLBACK MaxHzProc(HMONITOR mon, HDC, LPRECT, LPARAM lp) {
    MONITORINFOEXW mi;
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
        DEVMODEW dm;
        ZeroMemory(&dm, sizeof(dm));
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
            int hz = (int)dm.dmDisplayFrequency;
            int* best = reinterpret_cast<int*>(lp);
            if (hz > *best) *best = hz;
        }
    }
    return TRUE;
}

}

int HighestDisplayRefreshRate() {
    int best = 0;
    EnumDisplayMonitors(nullptr, nullptr, &MaxHzProc, reinterpret_cast<LPARAM>(&best));
    return best;
}

void LargestDisplaySize(uint32_t& width, uint32_t& height) {
    MaxMonitorDims mon;
    EnumDisplayMonitors(nullptr, nullptr, &AccumulateMaxMonitor, (LPARAM)&mon);
    if (mon.w == 0 || mon.h == 0) {
        mon.w = (uint32_t)GetSystemMetrics(SM_CXSCREEN);
        mon.h = (uint32_t)GetSystemMetrics(SM_CYSCREEN);
    }
    width  = mon.w;
    height = mon.h;
}

}

}
