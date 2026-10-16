#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

class SerialEndpoint;

/* Endpoint-facing surface of a serial UART, independent of the concrete engine:
   a PC-Card PC16550D (Serial16550) or an on-SoC UART implements it, and a
   SerialEndpoint personality pushes RX + modem-status back and reads the guest's
   line discipline through it without knowing which drives the wire. */
class SerialLine {
public:
    virtual ~SerialLine() = default;

    /* Personality -> RX. Any thread. Asserts the RX interrupt per the engine's
       enable/trigger state. */
    virtual void PushRx(const uint8_t* data, size_t n) = 0;

    /* True once the guest has read every queued RX byte. */
    virtual bool RxEmpty() const = 0;

    /* Fired off-lock when a guest read empties the RX queue, so a flow-
       controlled feeder can push the next frame. */
    using RxDrainFn = std::function<void()>;
    virtual void SetRxDrainCallback(RxDrainFn cb) = 0;

    /* Personality -> modem inputs (CTS/DSR/RI/DCD line levels). Any thread. */
    virtual void SetModemInputs(bool cts, bool dsr, bool ri, bool dcd) = 0;

    /* Decoded line discipline (baud + framing) the guest programmed, for a
       personality that mirrors it onto a real host port. */
    struct LineConfig {
        uint32_t baud      = 115200;
        uint8_t  data_bits = 8;   /* 5..8 */
        enum class Parity { None, Odd, Even, Mark, Space } parity = Parity::None;
        enum class Stop   { One, OnePointFive, Two }        stop   = Stop::One;

        /* Bit-times one character occupies on the wire: start + data + parity + stop. */
        double BitsPerChar() const {
            const double stop_bits = stop == Stop::Two          ? 2.0
                                   : stop == Stop::OnePointFive ? 1.5
                                                                : 1.0;
            return 1.0 + data_bits + (parity != Parity::None ? 1.0 : 0.0) + stop_bits;
        }
    };
    virtual LineConfig GetLineConfig() const = 0;

    /* Fired off-lock when the guest changes baud or framing, so a host-port
       forwarder can re-apply its settings live. */
    using LineConfigFn = std::function<void(const LineConfig&)>;
    virtual void SetLineConfigCallback(LineConfigFn cb) = 0;

    /* The personality the line delivers guest TX and control-line edges to. A
       PC-Card line binds this in its own ctor and leaves the default; an on-SoC
       UART takes it from the endpoint router on attach (nullptr on detach). */
    virtual void SetEndpoint(SerialEndpoint*) {}
};
