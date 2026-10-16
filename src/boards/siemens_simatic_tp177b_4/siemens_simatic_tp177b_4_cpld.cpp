#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/boards/board_context.h>
#include "siemens_simatic_tp177b_4_family_id.h"

#include <cstdint>

namespace cerfcore {

/* Siemens TP177B board CPLD on the S3C2410 nGCS4 bank (RE: nk.exe). */
namespace {

constexpr uint32_t kBase        = 0x20100000u;
constexpr uint32_t kSize        = 0x00200000u;  /* both nGCS4 glue sub-windows */
/* 16-bit write-only stage/heartbeat latch: startup STRH 0xF0/0xF2 around the
   DRAM clear (0x83008D9C/0x83008E04), tick heartbeat sub_8303EFAC; never read. */
constexpr uint32_t kCtrlReg     = 0x20100010u;
/* 16-bit board-rev; sole reader sub_830439E4 gates bits[15:8]==0x33. */
constexpr uint32_t kBoardRevReg = 0x2020030Eu;
constexpr uint16_t kBoardRev    = 0x3300u;

class SiemensSimaticTp177b4Cpld : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticTp177b4;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint16_t ReadHalf(uint32_t addr) override {
        if (addr == kBoardRevReg) return kBoardRev;
        HaltUnsupportedAccess("ReadHalf", addr, 0);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        if (addr == kCtrlReg) return;   /* write-only stage/heartbeat latch */
        HaltUnsupportedAccess("WriteHalf", addr, value);
    }
};

}  /* namespace */

REGISTER_SERVICE(SiemensSimaticTp177b4Cpld);

}
