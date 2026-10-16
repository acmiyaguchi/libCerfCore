#include "../../socs/pr31x00/pr31x00_serial_wiring.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "philips_velo_1_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* UART A ($0B0) is the built-in serial port (serial.dll HWInit sub_1EB2028). UART B
   ($0C8) is the debug module's port, reached only when the OAL debug flag is set, and
   is not an attach target. */
constexpr uint32_t kUartA = 0x10C000B0u;

/* CTS reads MFIODIN<30> and DCD IODIN<4> (GetModemStatus sub_1EB1F04); SET_RTS
   sub_1EB2F1C clears MFIODOUT<31> and SET_DTR sub_1EB2E30 clears IODOUT<3>. No DSR
   or RI is wired - sub_1EB1F04 reports only CTS and DCD. */
class PhilipsVelo1SerialWiring : public Pr31x00SerialWiring {
public:
    using Pr31x00SerialWiring::Pr31x00SerialWiring;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::PhilipsVelo1;
    }

    std::optional<Pr31x00SerialModem> ForUart(uint32_t mmio_base) const override {
        if (mmio_base != kUartA) return std::nullopt;
        Pr31x00SerialModem m;
        m.label = "COM1 (serial)";
        m.cts   = Pr31x00SerialPin::OnMfio(30);
        m.rts   = Pr31x00SerialPin::OnMfio(31);
        m.dcd   = Pr31x00SerialPin::OnIo(4);
        m.dtr   = Pr31x00SerialPin::OnIo(3);
        return m;
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(PhilipsVelo1SerialWiring, Pr31x00SerialWiring);

}
