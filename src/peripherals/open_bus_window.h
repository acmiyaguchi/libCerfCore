#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>

#include <cstdint>
#include <string_view>

namespace cerfcore {

class OpenBusWindow : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint8_t  ReadByte (uint32_t addr) override;
    uint16_t ReadHalf (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;

    void WriteByte (uint32_t addr, uint8_t  value) override;
    void WriteHalf (uint32_t addr, uint16_t value) override;
    void WriteWord (uint32_t addr, uint32_t value) override;

protected:
    virtual std::string_view WindowBoardFamilyId() const = 0;
    virtual const char*      WindowTag()     const = 0;

    void Trace(const char* op, uint32_t addr);
};

}
