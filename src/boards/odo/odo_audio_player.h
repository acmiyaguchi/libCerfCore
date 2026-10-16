#pragma once

#include <cerfcore/core/service.h>
#include "../../host/paced_wave_out.h"
#include "../../execution/guest_cycle_clock.h"
#include "../../socs/raster_scan_clock.h"

#include <cstdint>

namespace cerfcore {

class StateWriter;
class StateReader;

class OdoAudioPlayer : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    void SetPlaybackEnabled(bool enabled);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);
    void PostRestore();

private:
    void ResetLine();
    void CheckDacRate();
    void RequireScan(bool placed, const char* what);
    void ArmPageEnd(uint64_t now);
    void OnPageEnd();
    void QueuePage(uint32_t page, uint32_t first_sample);
    void OnRateChange();

    GuestCycleClock*        clock_   = nullptr;
    GuestCycleClock::Event* event_   = nullptr;
    NarrowRasterScanClock   scan_;
    PacedWaveOut            out_;
    bool                    playing_ = false;
};

}
