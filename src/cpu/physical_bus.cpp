#include "physical_bus.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/cpu/emulated_memory.h>

REGISTER_SERVICE(PhysicalBus);

bool PhysicalBus::Read(uint32_t pa, MmioWidth width, uint32_t* out) {
    if (uint8_t* p = emu_.Get<EmulatedMemory>().TryTranslate(pa)) {
        *out = static_cast<uint32_t>(cerf::le::UN(p, static_cast<size_t>(width)));
        return true;
    }
    auto& disp = emu_.Get<PeripheralDispatcher>();
    if (!disp.IsPeripheralAddress(pa)) return false;
    *out = disp.Read(pa, width);
    return true;
}

bool PhysicalBus::Write(uint32_t pa, MmioWidth width, uint32_t value) {
    if (uint8_t* p = emu_.Get<EmulatedMemory>().TryTranslateWrite(pa)) {
        cerf::le::PutN(p, value, static_cast<size_t>(width));
        return true;
    }
    auto& disp = emu_.Get<PeripheralDispatcher>();
    if (!disp.IsPeripheralAddress(pa)) return false;
    disp.Write(pa, value, width);
    return true;
}
