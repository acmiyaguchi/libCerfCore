#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "toshiba_gigabeat_s_family_id.h"
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>

namespace {

/* WEIM CS4 (PA 0xB4000000, 32 MB) - unidentified Zune 30 peripheral */
constexpr uint32_t kCs4Base = 0xB4000000u;
constexpr uint32_t kCs4Size = 0x02000000u;

class ToshibaGigabeatSCs4Stub : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::ToshibaGigabeatS;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kCs4Base; }
    uint32_t MmioSize() const override { return kCs4Size; }

    uint8_t  ReadByte (uint32_t addr) override { (void)addr; return 0xFFu; }
    uint16_t ReadHalf (uint32_t addr) override { (void)addr; return 0xFFFFu; }
    uint32_t ReadWord (uint32_t addr) override { (void)addr; return 0xFFFFFFFFu; }
    void WriteByte(uint32_t addr, uint8_t value) override { (void)addr; (void)value; }
    void WriteHalf(uint32_t addr, uint16_t value) override { (void)addr; (void)value; }
    void WriteWord(uint32_t addr, uint32_t value) override { (void)addr; (void)value; }
};

}  /* namespace */

REGISTER_SERVICE(ToshibaGigabeatSCs4Stub);
