#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/boards/board_context.h>
#include "siemens_simatic_tp177b_4_family_id.h"

#include <cstdint>

/* Siemens TP177B Profibus/MPI controller (ASPC2/CP5611-class) on nGCS1, modeled
   absent. The S7 driver (S7pbhmix.dll S7T_Init -> sub_33330C0) maps DPRAM
   @0x08040000 + regs @0x08020000, RAM-tests with pattern 0, gates on reg[0x0B]>=4;
   reads of 0 fail that gate, so the probe errors, S7T_Init returns NULL, device.exe skips it. */

namespace {

constexpr uint32_t kBase = 0x08000000u;  /* nGCS1 (OAT 0x86000000->0x08000000, 32 MB) */
constexpr uint32_t kSize = 0x02000000u;

class SiemensSimaticTp177b4Aspc2 : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticTp177b4;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    /* Reads 0, never open-bus 0xFFFFFFFF: a set bit makes the driver's pattern-0
       RAM test mismatch on the first word and changes the failure path. */
    uint8_t  ReadByte(uint32_t) override { return 0u; }
    uint16_t ReadHalf(uint32_t) override { return 0u; }
    uint32_t ReadWord(uint32_t) override { return 0u; }

    void WriteByte(uint32_t, uint8_t)  override {}
    void WriteHalf(uint32_t, uint16_t) override {}
    void WriteWord(uint32_t, uint32_t) override {}
};

}  /* namespace */

REGISTER_SERVICE(SiemensSimaticTp177b4Aspc2);
