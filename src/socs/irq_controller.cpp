#include "irq_controller.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

namespace cerfcore {

uint32_t IrqController::ReadPendingVector() {
    emu_.Get<Fatal>().Die(
        "irq controller: the core read a vectored-interrupt pending vector, "
        "but this interrupt controller has no vectored interface");
}

void IrqController::PulseIrq(int source_bit) {
    emu_.Get<Fatal>().Die(
        "irq controller: source %d delivered a zero-width pulse, and this "
        "interrupt controller has no modeled response to one", source_bit);
}

uint32_t CERFENGINE_FASTCALL IrqController::ReadPendingVectorHelper(void* intc) {
    return static_cast<IrqController*>(intc)->ReadPendingVector();
}

}
