#pragma once

#include "../raster_scan_peripheral.h"

#include <cstdint>

namespace cerfcore {

class Pr31x00Clock;
class Pr31x00Intc;

/* TMPR3911 §17.4.1-§17.4.4: Video Control 1-4 ($028-$034). */
class Pr31x00Lcd : public RasterScanPeripheral {
public:
    using RasterScanPeripheral::RasterScanPeripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x10C00028u; }
    uint32_t MmioSize() const override { return 0x10u; }

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    uint8_t  ReadByte(uint32_t addr) override { HaltUnsupportedAccess("PR31x00 LCD ReadByte", addr, 0); }
    uint16_t ReadHalf(uint32_t addr) override { HaltUnsupportedAccess("PR31x00 LCD ReadHalf", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("PR31x00 LCD WriteByte", addr, v); }
    void WriteHalf(uint32_t addr, uint16_t v) override { HaltUnsupportedAccess("PR31x00 LCD WriteHalf", addr, v); }

    /* Consumed by Pr31x00LcdRenderer. */
    bool     IsEnabled() const;
    bool     IsInverted() const;
    uint32_t GetFbPa() const;
    uint32_t GetGuestW() const;
    uint32_t GetGuestH() const;
    uint32_t GetBitsPerPixel() const;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

protected:
    ScanShape              ScanShapeLocked() const override;
    RasterScanClock::Frame ScanFrameLocked() const;
    void                   FrameEdgeLocked(uint32_t edge_index) override;
    void                   ScanEdgesRan() override;

private:
    void     WriteRegLocked(uint32_t addr, uint32_t value, uint64_t now);
    bool     ScanWantedLocked() const;
    void     SyncScanLocked(uint64_t now);
    uint32_t LineCntLocked(uint64_t now) const;
    void     WriteTimingLocked(uint32_t value, uint64_t now);
    void     LoadTimingLocked();
    void     ApplyLoadsLocked(uint64_t now);
    void     ArmLoadLocked();
    void     ScheduleFreezeLocked(uint64_t now);
    void     OnLoadEvent();
    void     RearmLoad();

    static constexpr uint32_t kRegs = 4;

    uint32_t reg_[kRegs] = {};

    Pr31x00Intc*            intc_       = nullptr;
    Pr31x00Clock*           clock_      = nullptr;
    GuestCycleClock*        cycles_     = nullptr;
    GuestCycleClock::Event* load_event_ = nullptr;

    uint32_t held_linecnt_       = 0;
    bool     frame_edge_pending_ = false;
    bool     enabled_edge_       = false;
    bool     cp_rate_changed_    = false;
    uint32_t vidrate_            = 0;
    uint32_t lineval_            = 0;
    bool     lineval_due_        = false;
    uint64_t lineval_tick_       = 0;
    bool     freeze_due_         = false;
    bool     freeze_stopped_     = false;
    uint64_t freeze_tick_        = 0;
};

}
