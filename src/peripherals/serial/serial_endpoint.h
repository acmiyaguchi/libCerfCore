#pragma once

#include <cstddef>
#include <cstdint>

#include "../../socs/uart_line_device.h"

namespace cerfcore {

class SerialLine;

class SerialEndpoint : public UartLineDevice {
public:
    virtual ~SerialEndpoint() = default;

    /* Bound once by the card before any traffic; the personality keeps the
       reference to push RX bytes and assert modem-status lines. */
    void BindUart(SerialLine& uart) { uart_ = &uart; }

    /* Guest wrote `n` bytes to THR (already de-FIFO'd by the UART). The modem
       parses these as AT / forwards them over PPP; GPS ignores them. */
    virtual void OnGuestTx(const uint8_t* data, size_t n) = 0;

    /* Guest changed the MCR control outputs. The modem treats a DTR 1->0 edge
       as hang-up; a personality that does not care leaves this default. */
    virtual void OnControlLines(bool dtr, bool rts) { (void)dtr; (void)rts; }

    /* Card power / socket-reset lifecycle. OnClose deasserts the line's modem inputs. */
    virtual void OnOpen()  {}
    virtual void OnClose() {}

    /* The line cleared its modem-status register on a reset; re-assert the inputs. */
    virtual void ResendModemInputs() {}

    virtual void PostRestore() {}

protected:
    SerialLine* uart_ = nullptr;
};

}
