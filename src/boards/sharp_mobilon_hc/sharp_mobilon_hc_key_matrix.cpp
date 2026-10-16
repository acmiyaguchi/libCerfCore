#include "sharp_mobilon_hc_key_matrix.h"

#include <cerfcore/boards/board_context.h>
#include "sharp_mobilon_hc_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../socs/pr31x00/pr31x00_io.h"

namespace cerfcore {

namespace {
/* Matrix interrupt line = MFIO pin 13 (SPINPOSINT, Interrupt Status 3 bit 13,
   tx39icureg.h:247) -> SYSINTR 8. Its EOI handler (nk.exe 0x91024244) delivers to
   keybddr only while MFIODIN ($18C) bit 13 reads high; a Status 3 bit set with the
   pin low is dropped (v0=0). */
constexpr uint32_t kSpinMfioPin = 13;
}  /* namespace */

bool SharpMobilonHcKeyMatrix::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpMobilonHc;
}

void SharpMobilonHcKeyMatrix::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint16_t SharpMobilonHcKeyMatrix::ReadHalf(uint32_t addr) {
    const uint32_t off = addr - kBase;
    uint16_t result = 0;
    for (uint8_t col = 0; col < 8; ++col)
        if (off & (2u << col))
            result |= rows_[col].load(std::memory_order_acquire);
    return result;
}

void SharpMobilonHcKeyMatrix::SetKey(uint8_t col, uint8_t row, bool down) {
    if (col >= 8 || row >= 16) return;
    const uint16_t bit = static_cast<uint16_t>(1u << row);

    const uint16_t prev = rows_[col].load(std::memory_order_acquire);
    const uint16_t next = down ? static_cast<uint16_t>(prev | bit)
                               : static_cast<uint16_t>(prev & ~bit);
    if (next == prev) return;
    rows_[col].store(next, std::memory_order_release);

    bool any = false;
    for (auto& r : rows_)
        if (r.load(std::memory_order_acquire)) { any = true; break; }

    emu_.Get<Pr31x00Io>().DriveMfioInput(kSpinMfioPin, any);
}

void SharpMobilonHcKeyMatrix::RestoreState(StateReader&) {
    for (auto& r : rows_) r.store(0, std::memory_order_release);
}

REGISTER_SERVICE(SharpMobilonHcKeyMatrix);

}
