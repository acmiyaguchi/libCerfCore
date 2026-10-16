#include "omap3530_dma_element_bus.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "omap3530_id.h"

namespace cerfcore {

namespace {

uint32_t ByteSwap(uint32_t value, uint32_t es) {
    switch (es) {
    case 1: return value & 0xFFu;
    case 2: return cerf::ByteSwap16(static_cast<uint16_t>(value));
    case 4: return cerf::ByteSwap32(value);
    default: return value;
    }
}

}

bool Omap3530DmaElementBus::ShouldRegister() {
    return emu_.Get<BoardContext>().GetSocId() == SocId::Omap3530;
}

void Omap3530DmaElementBus::OnReady() {
    mem_  = &emu_.Get<EmulatedMemory>();
    disp_ = &emu_.Get<PeripheralDispatcher>();
}

uint32_t Omap3530DmaElementBus::Read(uint32_t pa, uint32_t es, bool big_endian) {
    uint32_t value = 0;
    if (uint8_t* host = mem_->TryTranslate(pa)) {
        value = static_cast<uint32_t>(cerf::le::UN(host, es));
    } else {
        switch (es) {
        case 1: value = disp_->ReadByte(pa); break;
        case 2: value = disp_->ReadHalf(pa); break;
        case 4: value = disp_->ReadWord(pa); break;
        }
    }
    return big_endian ? ByteSwap(value, es) : value;
}

void Omap3530DmaElementBus::Write(uint32_t pa, uint32_t es, bool big_endian, uint32_t value) {
    const uint32_t v = big_endian ? ByteSwap(value, es) : value;
    if (uint8_t* host = mem_->TryTranslateWrite(pa)) {
        cerf::le::PutN(host, v, es);
        return;
    }
    switch (es) {
    case 1: disp_->WriteByte(pa, static_cast<uint8_t>(v)); break;
    case 2: disp_->WriteHalf(pa, static_cast<uint16_t>(v)); break;
    case 4: disp_->WriteWord(pa, v); break;
    }
}

REGISTER_SERVICE(Omap3530DmaElementBus);

}
