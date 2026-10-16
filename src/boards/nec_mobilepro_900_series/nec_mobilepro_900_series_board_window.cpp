#include "nec_mobilepro_900_series_board_window.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_900_series_family_id.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>

bool NecMobilepro900SeriesBoardWindow::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecMobilepro900Series;
}

void NecMobilepro900SeriesBoardWindow::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint8_t NecMobilepro900SeriesBoardWindow::ReadByte(uint32_t addr) {
    const uint32_t shift = (addr & 0x3u) * 8;
    return static_cast<uint8_t>((ReadReg(addr & ~0x3u) >> shift) & 0xFFu);
}

uint16_t NecMobilepro900SeriesBoardWindow::ReadHalf(uint32_t addr) {
    const uint32_t shift = (addr & 0x2u) * 8;
    return static_cast<uint16_t>((ReadReg(addr & ~0x3u) >> shift) & 0xFFFFu);
}

uint32_t NecMobilepro900SeriesBoardWindow::ReadWord(uint32_t addr) { return ReadReg(addr); }

void NecMobilepro900SeriesBoardWindow::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off   = (addr - MmioBase()) & ~0x3u;
    const uint32_t shift = (addr & 0x3u) * 8;
    uint32_t w = regs_.count(off) ? regs_[off] : 0u;
    w = (w & ~(0xFFu << shift)) | (static_cast<uint32_t>(value) << shift);
    regs_[off] = w;
    LOG(Caution, "NecBoardWindow[%s]: WriteByte PA 0x%08X (off 0x%X) = 0x%02X "
                 "(unmodeled, needs per-driver RE)\n",
        WindowName(), addr, off, value);
}

void NecMobilepro900SeriesBoardWindow::WriteHalf(uint32_t addr, uint16_t value) {
    const uint32_t off   = (addr - MmioBase()) & ~0x3u;
    const uint32_t shift = (addr & 0x2u) * 8;
    uint32_t w = regs_.count(off) ? regs_[off] : 0u;
    w = (w & ~(0xFFFFu << shift)) | (static_cast<uint32_t>(value) << shift);
    regs_[off] = w;
    LOG(Caution, "NecBoardWindow[%s]: WriteHalf PA 0x%08X (off 0x%X) = 0x%04X "
                 "(unmodeled, needs per-driver RE)\n",
        WindowName(), addr, off, value);
}

void NecMobilepro900SeriesBoardWindow::WriteWord(uint32_t addr, uint32_t value) {
    regs_[addr - MmioBase()] = value;
    LOG(Caution, "NecBoardWindow[%s]: WriteWord PA 0x%08X (off 0x%X) = 0x%08X "
                 "(unmodeled, needs per-driver RE)\n",
        WindowName(), addr, addr - MmioBase(), value);
}

uint32_t NecMobilepro900SeriesBoardWindow::ReadReg(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    auto it = regs_.find(off);
    const uint32_t v = (it != regs_.end()) ? it->second : 0u;
    LOG(Caution, "NecBoardWindow[%s]: read PA 0x%08X (off 0x%X) -> 0x%08X "
                 "(unmodeled, needs per-driver RE)\n",
        WindowName(), addr, off, v);
    return v;
}

void NecMobilepro900SeriesBoardWindow::SaveState(StateWriter& w) {
    w.Write("regs_count", static_cast<uint32_t>(regs_.size()));
    for (const auto& [off, val] : regs_) { w.Write("off", off); w.Write("val", val); }
}

void NecMobilepro900SeriesBoardWindow::RestoreState(StateReader& r) {
    regs_.clear();
    uint32_t n = 0;
    r.Read("regs_count", n);
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t off = 0, val = 0;
        r.Read("off", off);
        r.Read("val", val);
        regs_[off] = val;
    }
}
