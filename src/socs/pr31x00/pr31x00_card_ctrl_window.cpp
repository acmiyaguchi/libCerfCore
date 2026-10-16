#include "pr31x00_card_space.h"

#include <cerfcore/boards/board_context.h>
#include "pr31500_id.h"
#include "pr31700_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* Card 1 and Card 2 I/O-or-Attribute space, 64 MB each, adjacent (Table 4.2.1). */
constexpr uint32_t kBase = 0x08000000u;
constexpr uint32_t kSize = 0x08000000u;

class Pr31x00CardCtrlWindow : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd) return false;
        const std::string_view soc = bd->GetSocId();
        return soc == SocId::Pr31500 || soc == SocId::Pr31700;
    }

    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint8_t ReadByte(uint32_t addr) override {
        return emu_.Get<Pr31x00CardSpace>().ReadCtrl8(addr - kBase);
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return emu_.Get<Pr31x00CardSpace>().ReadCtrl16(addr - kBase);
    }
    /* A 16-bit PC Card bus has no 32-bit cycle; the BIU runs two halfword cycles. */
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t lo = ReadHalf(addr);
        const uint32_t hi = ReadHalf(addr + 2u);
        return lo | (hi << 16);
    }

    void WriteByte(uint32_t addr, uint8_t value) override {
        emu_.Get<Pr31x00CardSpace>().WriteCtrl8(addr - kBase, value);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        emu_.Get<Pr31x00CardSpace>().WriteCtrl16(addr - kBase, value);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        WriteHalf(addr, static_cast<uint16_t>(value));
        WriteHalf(addr + 2u, static_cast<uint16_t>(value >> 16));
    }
};

}  /* namespace */

REGISTER_SERVICE(Pr31x00CardCtrlWindow);

}
