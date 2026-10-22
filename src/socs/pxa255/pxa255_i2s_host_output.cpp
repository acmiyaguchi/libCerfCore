#include "pxa255_i2s_host_output.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "pxa255_id.h"

namespace cerfcore {

namespace {

/* Intel PXA255 Developer's Manual section 14.2 (page 14-2): SYNC is BITCLK divided by 64. */
constexpr uint64_t kBitsPerFrame = 64u;

}  // namespace

REGISTER_SERVICE(Pxa255I2sHostOutput);

bool Pxa255I2sHostOutput::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Pxa255;
}

void Pxa255I2sHostOutput::OnReady() { session_.Open(); }

void Pxa255I2sHostOutput::OnShutdown() { session_.Close(); }

void Pxa255I2sHostOutput::Queue(const void* bytes, uint32_t length, GuestCycleClock::Rate bit_rate) {
    const uint32_t rate = static_cast<uint32_t>(bit_rate.Scaled(1u, kBitsPerFrame).RoundedHz());
    if (rate == 0u) return;
    session_.Queue(bytes, length, rate);
}

}
