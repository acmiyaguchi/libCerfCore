#include "../../peripherals/uart16550/uart16550.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "sharp_mobilon_hc_family_id.h"
#include "sharp_mobilon_hc_cmtt.h"

#include <cstdint>

namespace cerfcore {

namespace {

class SharpMobilonHcDebugUart : public Uart16550 {
public:
    explicit SharpMobilonHcDebugUart(CerfEmulator& emu)
        : Uart16550(emu, Config{}) {}

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpMobilonHc;
    }

    uint32_t MmioBase() const override { return 0x10800000u; }
    uint32_t MmioSize() const override { return 0x100u; }

    /* 16550 reg0..7 at CS3+0x20 stride 4 (sub_9102151C @0x9102151C, base $v1=0xB0800020). */
    uint8_t ReadByte(uint32_t addr) override {
        uint32_t idx;
        if (CoreIndex(addr, idx)) return ReadReg8(idx);
        /* CS3+0x03 = Cmtt status byte (sub_910236E4). */
        if (addr - MmioBase() == 0x03u) {
            return emu_.Get<SharpMobilonHcCmtt>().ReadStatusByte();
        }
        HaltUnsupportedAccess("ReadByte", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        uint32_t idx;
        if (CoreIndex(addr, idx)) { WriteReg8(idx, value); return; }
        /* +0x42 = 0x44 each debug op (sub_910215C4 @0x910215C4, sub_91021620 @0x91021620). */
        if (addr - MmioBase() == 0x42u) {
            if (value != 0x44u) HaltUnsupportedAccess("CS3 +0x42 chip byte", addr, value);
            return;
        }
        HaltUnsupportedAccess("WriteByte", addr, value);
    }

    /* CS3+0x00 = Cmtt storage-companion data/command port (sub_91023A40). */
    void WriteWord(uint32_t addr, uint32_t value) override {
        if (addr - MmioBase() == 0x00u) {
            emu_.Get<SharpMobilonHcCmtt>().WriteDataPort(value);
            return;
        }
        HaltUnsupportedAccess("WriteWord", addr, value);
    }
    uint32_t ReadWord(uint32_t addr) override {
        if (addr - MmioBase() == 0x00u) {
            return emu_.Get<SharpMobilonHcCmtt>().ReadDataPort();
        }
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }

protected:
    uint32_t    RegStride() const override { return 4u; }
    const char* Name()      const override { return "CS3DBG"; }

    /* OAL leaves IER 0, port is polled (sub_9102151C @0x9102151C). */
    void SetInterruptLine(bool pending) override {
        if (!pending) return;
        LOG(Caution, "CS3 debug UART raised an interrupt; no INTC routing modeled\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

private:
    static constexpr uint32_t kCoreOff = 0x20u;

    bool CoreIndex(uint32_t addr, uint32_t& idx) const {
        const uint32_t off = addr - MmioBase();
        if (off < kCoreOff) return false;
        const uint32_t rel = off - kCoreOff;
        if (rel % RegStride() != 0u) return false;
        const uint32_t i = rel / RegStride();
        if (i >= 8u) return false;
        idx = i;
        return true;
    }
};

}  /* namespace */

REGISTER_SERVICE(SharpMobilonHcDebugUart);

}
