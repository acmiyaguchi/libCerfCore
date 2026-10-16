#pragma once

#include "../../lcd/display_mode_latch.h"
#include "../../socs/raster_scan_peripheral.h"
#include "mediaq_mq200_fp.h"
#include "mediaq_mq200_gc.h"
#include "mediaq_mq200_ge.h"
#include "mediaq_mq200_ge_window.h"
#include "mediaq_mq200_interrupts.h"
#include "mediaq_mq200_memory.h"
#include "mediaq_mq200_panel.h"
#include "mediaq_mq200_power.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

class MediaQMq200 : public RasterScanPeripheral, public MediaQGeHost {
public:
    using RasterScanPeripheral::RasterScanPeripheral;
    using HwCursor = MediaQMq200Panel::HwCursor;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return mmio_base_; }
    uint32_t MmioSize() const override { return kRegWinOff + kRegSize; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint16_t ReadHalf (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

    bool     IsEnabled()      const { return panel_.Shown(fb_blocked_ == nullptr); }
    uint32_t Bpp()            const { return panel_.Bpp(); }
    uint32_t GetGuestW()      const { return panel_.Width(); }
    uint32_t GetGuestH()      const { return panel_.Height(); }
    uint32_t FbWindowOffset() const { return panel_.WindowOffset(); }
    uint32_t Stride()         const { return panel_.Stride(); }
    uint32_t PaletteEntry(uint32_t index) const { return panel_.Palette(index); }
    HwCursor PanelCursor()    const { return panel_.Cursor(fb_blocked_ == nullptr); }
    MediaQMq200Gc::Window PanelImage() const { return panel_.Image(); }
    const uint8_t* FbBytes()  const { return fb_.data(); }
    uint8_t*       FbMutableBytes() override { return fb_.data(); }
    uint32_t       FbSize()   const override { return kFbSize; }

private:
    /* simpad_sl4_ce4_10 ddi.dll sub_134BAA8: a1[65] frame buffer size, a1[72] = a1[71] +
       0x600000 register window, a1[66] register window size. */
    static constexpr uint32_t kFbSize    = 0x200000u;
    static constexpr uint32_t kRegWinOff = 0x600000u;
    static constexpr uint32_t kRegSize   = 0x1A000u;

    enum class Gc1Scan : uint8_t { Off = 0, Waiting = 1, Live = 2, Held = 3, Unmodeled = 4 };

    static bool     PartialWriteAllowed(uint32_t roff);

    void     ResetChipLocked();
    void     OnPowerOnReset();

    uint32_t RegRead(uint32_t roff);
    void     RegWrite(uint32_t roff, uint32_t value);
    uint32_t RegReadLocked(uint32_t roff);
    bool     RegWriteLocked(uint32_t roff, uint32_t value);
    bool     FbReadServed(uint32_t addr) const;
    bool     FbWriteServed(uint32_t addr, uint32_t value, uint32_t width);
    bool     SettleLocked();
    void     Settle(bool rescale);
    void     RefreshGatesLocked();
    void     CheckGc1ObservedLocked() const;

    bool      UpdateGc1ScanLocked();
    void      RestoreGc1WhyLocked();
    uint32_t  Gc1VerticalCountLocked();

    ScanShape ScanShapeLocked() const override;
    bool      EdgeRaisesInterruptLocked(uint32_t, bool) const override;
    bool      FrameEdgesInertLocked() const override;
    void      FrameEdgeLocked(uint32_t edge_index) override;
    void      ScanEdgesRan() override {}

    void PublishScreenSizeOnEnableEdge();

    uint32_t              mmio_base_ = 0;
    std::vector<uint8_t>  fb_;
    uint32_t              cc00_ = 0;
    MediaQMq200Power      power_{emu_};
    MediaQMq200Memory     memory_{emu_, power_};
    MediaQMq200Interrupts in_{emu_};
    MediaQMq200Gc         gc_{emu_};
    MediaQMq200Fp         fp_{emu_};
    MediaQMq200Panel      panel_{power_, fp_, gc_};
    DisplayModeLatch      mode_latch_;
    MediaQMq200Ge         ge_{*this};
    MediaQMq200GeWindow   ge_window_{emu_, ge_};

    const char* fb_blocked_ = nullptr;
    const char* ge_blocked_ = nullptr;
    bool        ge_powered_ = false;

    Gc1Scan                   gc1_scan_        = Gc1Scan::Off;
    MediaQMq200Gc::ScanTiming gc1_applied_;
    uint32_t                  gc1_held_vcount_ = 0;
};

}
