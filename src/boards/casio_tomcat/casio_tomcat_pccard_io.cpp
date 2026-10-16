#include <cerfcore/peripherals/peripheral_base.h>

#include "casio_tomcat_pccard.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/boards/board_context.h>
#include "casio_tomcat_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* casio_cassiopeia_e55 pcmcia.dll sub_14C1604 @0x14C1604 fills the descriptor's second
   region with 0xB400C000-0xB400C7FF on both straps; NetBSD sys/arch/hpcmips/conf/VR41XX
   gives the Cassiopeia E series isaportoffset 0xc000 with wdc0 at port 0x170, so a window
   offset is the card's I/O address. */
constexpr uint32_t kIoBase = 0x1400C000u;
constexpr uint32_t kIoSize = 0x800u;

class CasioTomcatPcCardIo : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::CasioTomcat;
    }

    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kIoBase; }
    uint32_t MmioSize() const override { return kIoSize; }

    uint8_t ReadByte(uint32_t addr) override {
        return emu_.Get<CasioTomcatPcCard>().ReadIo8(addr - kIoBase);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        emu_.Get<CasioTomcatPcCard>().WriteIo8(addr - kIoBase, value);
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return emu_.Get<CasioTomcatPcCard>().ReadIo16(addr - kIoBase);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        emu_.Get<CasioTomcatPcCard>().WriteIo16(addr - kIoBase, value);
    }
};

}  /* namespace */

REGISTER_SERVICE(CasioTomcatPcCardIo);

}
