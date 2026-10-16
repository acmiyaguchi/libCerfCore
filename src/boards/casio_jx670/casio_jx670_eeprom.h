#pragma once

#include <cstdint>

namespace cerfcore {

class StateWriter;
class StateReader;

/* 93Cxx serial EEPROM bit-banged by eeprom.dll over the companion 0xA100 block
   (mapper base 0x0A00A100): CS sub_F2196C @0xF2196C (base+0), CLK sub_F2197C
   @0xF2197C (base+4), DI sub_F219EC @0xF219EC (base+8), DO sub_F219D4 @0xF219D4
   (base+0x14); clocked by sub_F2175C @0xF2175C. */
class CasioJx670Eeprom {
public:
    bool TryReadWord (uint32_t off, uint32_t& out);
    bool TryWriteWord(uint32_t off, uint32_t  value);

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    /* eeprom.dll sub_F21A54 @0xF21A54 (|=2), sub_F21A6C @0xF21A6C (|=1),
       sub_F21A84 @0xF21A84 (&~1) RMW read-back (companion 0xA110). */
    uint32_t ctrl_a110_ = 0;
    /* eeprom.dll sub_F2198C @0xF2198C / @0xF219A4 / @0xF219BC RMW read-back
       (companion 0xA118). */
    uint32_t ctrl_a118_ = 0;
};

}
