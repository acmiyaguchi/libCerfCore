#include "../../peripherals/uart16550/uart16550.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_700_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* OAL debug serial port, stride-2 16550 (nk.exe sub_9F00282C: probes SCR at 0x1600FFCE with
   0x55/0xAA, then programs FCR=7 DLL=3 DLM=0 LCR=3 MCR=0x0B). Its TX carries
   OEMWriteDebugString - the kernel banner and OAL diagnostics - so this port IS the board's
   NKDBG channel, not one of serial.dll's three COM devices. */
class Vr4102ComUart : public Uart16550 {
public:
    explicit Vr4102ComUart(CerfEmulator& emu)
        : Uart16550(emu, Config{/*ier_mask=*/0x0Fu,
                                /*irq_gate_mcr=*/0u,
                                /*irq_gate_ier=*/0u}) {}

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecMobilepro700;
    }

    uint32_t MmioBase() const override { return 0x1600FFC0u; }
    uint32_t MmioSize() const override { return 0x10u; }

protected:
    uint32_t    RegStride() const override { return 2u; }
    const char* Name()      const override { return "COM"; }

    /* The OAL leaves IER 0 (sub_9F00282C writes DLM=0 and never enables an interrupt), so
       the port is polled and no line should ever be raised. */
    void SetInterruptLine(bool pending) override {
        if (!pending) return;
        LOG(Caution, "COM UART raised an interrupt; interrupt routing to the ICU "
                     "is not implemented\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
};

}  /* namespace */

REGISTER_SERVICE(Vr4102ComUart);

}
