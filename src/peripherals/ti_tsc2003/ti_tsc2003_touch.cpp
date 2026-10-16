#include "ti_tsc2003_touch.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/ford_sync_gen2/ford_sync_gen2_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>

#include <utility>

namespace cerfcore {

REGISTER_SERVICE(Tsc2003Touch);

bool Tsc2003Touch::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::FordSyncGen2;
}

void Tsc2003Touch::SetState(uint16_t adc_x, uint16_t adc_y, bool pen_down) {
    adc_x_.store(static_cast<uint16_t>(adc_x & 0x0FFFu), std::memory_order_relaxed);
    adc_y_.store(static_cast<uint16_t>(adc_y & 0x0FFFu), std::memory_order_relaxed);
    pen_down_.store(pen_down, std::memory_order_release);
    UpdatePenIrq();
}

void Tsc2003Touch::SetPenIrqObserver(std::function<void(bool)> cb) {
    penirq_obs_ = std::move(cb);
}

void Tsc2003Touch::UpdatePenIrq() {
    std::lock_guard<std::mutex> lk(penirq_mu_);
    const bool now = PenIrqAsserted();
    if (now == last_penirq_) return;
    last_penirq_ = now;
    if (penirq_obs_) penirq_obs_(now);
}

bool Tsc2003Touch::PenIrqAsserted() const {
    return penirq_en_.load(std::memory_order_acquire) &&
           pen_down_.load(std::memory_order_acquire);
}

void Tsc2003Touch::WriteCommand(uint8_t cmd) {
    const uint8_t c  = static_cast<uint8_t>((cmd >> 4) & 0x0Fu);  /* C3-C0 (Fig.11) */
    const uint8_t pd = static_cast<uint8_t>((cmd >> 2) & 0x03u);  /* PD1-PD0 (Fig.11) */
    /* PENIRQ enabled iff PD0==0 (datasheet Table II: PD=00/10 Enabled, 01/11 Disabled). */
    penirq_en_.store((pd & 0x1u) == 0u, std::memory_order_release);
    read_idx_ = 0;
    switch (c) {                        /* datasheet Table I converter functions */
    case 0xCu:                          /* Measure X Position */
        result_ = adc_x_.load(std::memory_order_relaxed);
        result_valid_ = true;
        break;
    case 0xDu:                          /* Measure Y Position */
        result_ = adc_y_.load(std::memory_order_relaxed);
        result_valid_ = true;
        break;
    case 0xAu:                          /* Activate Y+/X- drivers (touch settle) */
    case 0x0u:                          /* driver's power-down/standby write (0x00) */
        result_valid_ = false;
        break;
    default:
        /* touch_tsc2003.dll issues only C in {0x0,0xA,0xC,0xD}; a temp/batt/aux/Z
           conversion is unmodeled - fail loud so it self-reveals. */
        LOG(Caution, "Tsc2003Touch: unmodeled command 0x%02X (C=0x%X)\n", cmd, c);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    UpdatePenIrq();
}

uint8_t Tsc2003Touch::ReadResultByte() {
    if (!result_valid_) {
        LOG(Caution, "Tsc2003Touch: read with no pending X/Y conversion\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    /* 12-bit result, MSB-first; the driver recombines (byte0<<4)|(byte1>>4)
       (sub_C1471EA0): byte0 = D[11:4], byte1 = D[3:0] in the top nibble. */
    uint8_t out;
    if (read_idx_ == 0u) {
        out = static_cast<uint8_t>(result_ >> 4);
    } else if (read_idx_ == 1u) {
        out = static_cast<uint8_t>((result_ & 0x0Fu) << 4);
    } else {
        LOG(Caution, "Tsc2003Touch: read byte %u beyond the 2-byte result\n", read_idx_);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    ++read_idx_;
    return out;
}

}
