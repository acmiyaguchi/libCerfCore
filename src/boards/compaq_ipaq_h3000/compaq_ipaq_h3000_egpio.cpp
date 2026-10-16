#include "compaq_ipaq_h3000_egpio.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "compaq_ipaq_h3000_family_id.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include "compaq_ipaq_h3000_egpio_sink.h"

namespace cerfcore {

bool CompaqIpaqH3000Egpio::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::CompaqIpaqH3000;
}

void CompaqIpaqH3000Egpio::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void CompaqIpaqH3000Egpio::NotifySink() {
    if (auto* sink = emu_.TryGet<CompaqIpaqH3000EgpioSink>()) {
        sink->OnEgpioChanged(latched_.load(std::memory_order_acquire));
    }
}

void CompaqIpaqH3000Egpio::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off = addr - MmioBase();
    if (off > 1u) HaltUnsupportedAccess("WriteByte", addr, value);
    const uint32_t shift  = 8u * off;
    const uint16_t mask   = static_cast<uint16_t>(0xFFu << shift);
    const uint16_t before = latched_.load(std::memory_order_acquire);
    const uint16_t after  =
        static_cast<uint16_t>((before & ~mask) | (static_cast<uint32_t>(value) << shift));
    latched_.store(after, std::memory_order_release);
    LOG(Periph, "[CompaqIpaqH3000Egpio] W8 +%u = 0x%02X -> latch 0x%04X\n",
        off, value, after);
    NotifySink();
}

/* Halfword store of a uint16_t shadow: NetBSD
   sys/arch/hpcarm/dev/ipaq_saip.c ipaq_attach. */
void CompaqIpaqH3000Egpio::WriteHalf(uint32_t addr, uint16_t value) {
    StoreLatch("WriteHalf", addr, value);
}

void CompaqIpaqH3000Egpio::WriteWord(uint32_t addr, uint32_t value) {
    StoreLatch("WriteWord", addr, value);
}

void CompaqIpaqH3000Egpio::StoreLatch(const char* op, uint32_t addr, uint32_t value) {
    if (addr != MmioBase()) HaltUnsupportedAccess(op, addr, value);
    const uint16_t latched = static_cast<uint16_t>(value);
    latched_.store(latched, std::memory_order_release);
    LOG(Periph, "[CompaqIpaqH3000Egpio] %s = 0x%04X\n", op, latched);
    NotifySink();
}

void CompaqIpaqH3000Egpio::SaveState(StateWriter& w) {
    w.Write("latched", latched_.load(std::memory_order_acquire));
}

void CompaqIpaqH3000Egpio::RestoreState(StateReader& r) {
    uint16_t v = 0;
    r.Read("latched", v);
    latched_.store(v, std::memory_order_release);
}

REGISTER_SERVICE(CompaqIpaqH3000Egpio);

}
