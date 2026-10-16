#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/host/host_widget.h>

#include <cstdint>
#include <string>
#include <vector>

namespace cerfcore {

class StateWriter;
class StateReader;

/* Shared AMD/JEDEC command-set NOR flash FSM. Commands are matched on their low
   byte so single-byte (0xAA) and bus-doubled (0xAAAA) driver forms both work;
   mutations are applied to EmulatedMemory immediately (reads bypass this
   peripheral), so the guest's DQ-toggle poll reads stable data and completes. */
class AmdCommandSetFlash : public Peripheral, public HostWidget {
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

    /* HostWidget - generic NOR icon; concretes override the label/tooltip. */
    std::string WidgetName() const override { return "NOR Flash"; }
    WidgetGroup Group()      const override { return WidgetGroup::Storage; }
    std::string Tooltip()    const override { return "NOR Flash"; }
    std::vector<WidgetControl> Controls() override;
    WidgetIcon  Icon()       const override { return ChipIcon{}; }

protected:
    /* 32-bit word returned at flash offset 0 while in autoselect mode:
       low 16 = manufacturer id, high 16 = device id. */
    virtual uint32_t AutoSelectIdent() const = 0;
    /* Byte offsets of the two AMD unlock cycles (0xAAA/0x554 on a standard x16
       part; 0xAAAA/0x5554 on the larger-decode parts). */
    virtual uint32_t UnlockAddr1() const = 0;
    virtual uint32_t UnlockAddr2() const = 0;
    /* Erase-sector size for the sector containing io_addr. */
    virtual uint32_t SectorSize(uint32_t io_addr) const = 0;

private:
    enum class St : uint8_t {
        Read, Unlock1, Unlock2, Program,
        EraseSetup, EraseUnlock1, EraseUnlock2,
        AutoSelect, BypassProgram, BypassExit,
    };

    void DoWriteHalf(uint32_t io_addr, uint16_t value);
    void EnterAutoSelect();
    void LeaveAutoSelect();

    St       st_           = St::Read;
    bool     bypass_       = false;   /* unlock-bypass mode active */
    uint32_t cached_word0_ = 0u;      /* flash word0 saved across autoselect */
};

}
