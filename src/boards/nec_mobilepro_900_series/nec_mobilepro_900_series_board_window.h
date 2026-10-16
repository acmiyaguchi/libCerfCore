#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cstdint>
#include <unordered_map>

namespace cerfcore {

/* Unmodeled-device scaffold (NOT a model): stores writes, returns stored/0 on
   reads, logs every access loud (CAUTION). Each access is a peripheral CERF does
   not model; a flood of one offset is a guest busy-waiting on a status bit this
   scaffold never sets - a hang. Concrete supplies MmioBase()+WindowName(). */
class NecMobilepro900SeriesBoardWindow : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    /* OAT board-device bands are 1 MB each. A concrete with a different span
       overrides this. */
    uint32_t MmioSize() const override { return 0x00100000u; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint16_t ReadHalf (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

protected:
    /* Short identifier for this window in the unmodeled-access logs. */
    virtual const char* WindowName() const = 0;

private:
    uint32_t ReadReg(uint32_t addr);

    std::unordered_map<uint32_t, uint32_t> regs_;
};

}
