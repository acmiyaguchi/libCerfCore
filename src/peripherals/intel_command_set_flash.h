#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace cerfcore {

class StateWriter;
class StateReader;

class IntelCommandSetFlash : public Peripheral {
public:
    using Peripheral::Peripheral;

    void OnReady() override;

    uint8_t  ReadByte (uint32_t addr) override;
    uint16_t ReadHalf (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

protected:
    virtual uint16_t Manufacturer() const = 0;
    virtual uint16_t Device()       const = 0;

    virtual uint32_t Parallel()    const = 0;
    virtual uint32_t DeviceWidth() const = 0;
    virtual uint32_t ChipEraseBlockBytes() const = 0;

    virtual const uint8_t* CfiTable() const { return nullptr; }
    virtual uint32_t       CfiCount() const { return 0u; }

    virtual uint8_t DecodeCommand(uint32_t value, uint32_t width);
    virtual bool    CommandEnabled(uint8_t) const { return true; }
    virtual void    PresentIdentifier(uint32_t addr);

    void     SaveAndWrite(uint32_t addr, uint8_t v);
    void     SaveAndWriteWord(uint32_t addr, uint32_t word);
    void     RestoreArray();
    void     EmitLaneByte(uint32_t addr, uint8_t v);
    uint8_t* Host(uint32_t addr);

    uint32_t BusBytes()        const { return Parallel() * DeviceWidth(); }
    uint32_t EraseBlockBytes() const { return ChipEraseBlockBytes() * Parallel(); }
    uint32_t NumBlocks()       const { return MmioSize() / EraseBlockBytes(); }
    uint32_t BlockBase(uint32_t i)     const { return MmioBase() + i * EraseBlockBytes(); }
    uint32_t BlockIndex(uint32_t addr) const { return (addr - MmioBase()) / EraseBlockBytes(); }
    uint32_t BlockIndexChecked(uint32_t addr);

    /* 28F128J3 datasheet (Intel order 290667) Table 6: SR7 ready, SR5 erase/
       clear-lock error, SR4 program/set-lock error, SR1 lock-detect abort. */
    static constexpr uint8_t  kSrReady     = 0x80u;
    static constexpr uint8_t  kSrEraseErr  = 0x20u;
    static constexpr uint8_t  kSrProgErr   = 0x10u;
    static constexpr uint8_t  kSrLockAbort = 0x02u;
    static constexpr uint32_t kCfiFirst    = 0x10u;
    /* SIMpad FLite probe reads the block lock bit at block_base + 8
       (psmfsd sub_12AD330, 0x12AD854 `ADD R0,R6,#8`). */
    static constexpr uint32_t kLockReadOffset = 8u;

private:
    enum class Mode : uint8_t {
        kArray, kProgramSetup, kEraseSetup, kLockSetup,
        kWriteBufCount, kWriteBufData, kWriteBufConfirm,
    };

    void PresentStatus(uint32_t addr, uint8_t sr = kSrReady);
    void PresentCfi();
    bool ProgramInto(uint32_t addr, uint32_t value, uint32_t width);
    void CommitProgram(uint32_t addr, uint32_t value, uint32_t width);
    void EraseBlock(uint32_t addr);
    void Command(uint32_t addr, uint32_t value, uint32_t width);

    Mode     mode_          = Mode::kArray;
    uint32_t buf_remaining_ = 0u;
    std::vector<std::pair<uint32_t, uint8_t>> shadow_;
    std::vector<uint8_t> block_locked_;
};

}
