#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cstdint>

class Sa1111ResetLine;
class Sa1111Sbi;
class Sa1111SystemController;

class Sa1111Unit : public Peripheral {
public:
    using Peripheral::Peripheral;

    void OnReady() final;

    uint8_t  ReadByte (uint32_t addr) final;
    uint16_t ReadHalf (uint32_t addr) final;
    uint32_t ReadWord (uint32_t addr) final;
    uint64_t ReadDword(uint32_t addr) final;
    void     WriteByte (uint32_t addr, uint8_t  value) final;
    void     WriteHalf (uint32_t addr, uint16_t value) final;
    void     WriteWord (uint32_t addr, uint32_t value) final;
    void     WriteDword(uint32_t addr, uint64_t value) final;

    FastReadFn  FastReader() final;
    FastWriteFn FastWriter() final;

protected:
    virtual void OnUnitReady() {}
    virtual void OnChipReset(bool held) = 0;
    virtual bool NeedsRclk(uint32_t) const { return true; }
    bool ChipHeld() const;

    virtual uint8_t  UnitReadByte (uint32_t addr);
    virtual uint16_t UnitReadHalf (uint32_t addr);
    virtual uint32_t UnitReadWord (uint32_t addr);
    virtual uint64_t UnitReadDword(uint32_t addr);
    virtual void     UnitWriteByte (uint32_t addr, uint8_t  value);
    virtual void     UnitWriteHalf (uint32_t addr, uint16_t value);
    virtual void     UnitWriteWord (uint32_t addr, uint32_t value);
    virtual void     UnitWriteDword(uint32_t addr, uint64_t value);

private:
    bool Admit(uint32_t addr);

    Sa1111ResetLine*        reset_line_ = nullptr;
    Sa1111Sbi*              sbi_        = nullptr;
    Sa1111SystemController* controller_ = nullptr;
};
