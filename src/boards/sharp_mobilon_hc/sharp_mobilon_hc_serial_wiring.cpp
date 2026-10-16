#include "../../socs/pr31x00/pr31x00_serial_wiring.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "sharp_mobilon_hc_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* UART A ($0B0) is the built-in serial port (serial.dll HWInit sub_14755F0,
   config index 0). CTS=IODIN<6> DCD=IODIN<4> (GetModemStatus sub_1474830:
   bit6->EV_CTS, bit4->EV_DSR|EV_RLSD); SET_DTR sub_1474EAC clears MFIODOUT<12>,
   SET_RTS sub_1474F48 clears IODOUT<5> (IO_CTL bit 0x2000). */
constexpr uint32_t kUartA = 0x10C000B0u;

class SharpMobilonHcSerialWiring : public Pr31x00SerialWiring {
public:
    using Pr31x00SerialWiring::Pr31x00SerialWiring;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpMobilonHc;
    }

    std::optional<Pr31x00SerialModem> ForUart(uint32_t mmio_base) const override {
        if (mmio_base != kUartA) return std::nullopt;
        Pr31x00SerialModem m;
        m.label = "COM1 (serial)";
        m.cts   = Pr31x00SerialPin::OnIo(6);
        m.dcd   = Pr31x00SerialPin::OnIo(4);
        m.dtr   = Pr31x00SerialPin::OnMfio(12);
        m.rts   = Pr31x00SerialPin::OnIo(5);
        return m;
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(SharpMobilonHcSerialWiring, Pr31x00SerialWiring);

}
