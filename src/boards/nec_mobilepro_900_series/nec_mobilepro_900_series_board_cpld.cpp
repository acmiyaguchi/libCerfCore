#include "nec_mobilepro_900_series_board_window.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>

namespace {

/* CS3 board CPLD (PA 0x13000000). Offset 0x80000 (the OAL table's "NAND" entry) is
   the OST tick heartbeat: nk.exe's tick ISR (sub_9023B758) writes a per-tick counter
   there every tick, never reading it back. Modeled as a write-accepting scratch
   register; a read returns the last write. */
class NecMobilepro900SeriesBoardCpld : public NecMobilepro900SeriesBoardWindow {
public:
    using NecMobilepro900SeriesBoardWindow::NecMobilepro900SeriesBoardWindow;

    uint32_t MmioBase() const override { return 0x13000000u; }

    uint32_t ReadWord(uint32_t addr) override {
        if (addr - MmioBase() == kTickHeartbeatOff) return heartbeat_;
        return NecMobilepro900SeriesBoardWindow::ReadWord(addr);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        if (addr - MmioBase() == kTickHeartbeatOff) { heartbeat_ = value; return; }
        NecMobilepro900SeriesBoardWindow::WriteWord(addr, value);
    }

    void SaveState(StateWriter& w) override {
        NecMobilepro900SeriesBoardWindow::SaveState(w);
        w.Write("heartbeat", heartbeat_);
    }
    void RestoreState(StateReader& r) override {
        NecMobilepro900SeriesBoardWindow::RestoreState(r);
        r.Read("heartbeat", heartbeat_);
    }

protected:
    const char* WindowName() const override { return "sysctl-cpld@0x13000000"; }

private:
    static constexpr uint32_t kTickHeartbeatOff = 0x80000u;  /* PA 0x13080000 */
    uint32_t heartbeat_ = 0;
};

}  /* namespace */

REGISTER_SERVICE(NecMobilepro900SeriesBoardCpld);
