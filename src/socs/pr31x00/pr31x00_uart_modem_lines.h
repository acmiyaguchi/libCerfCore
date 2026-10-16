#pragma once

#include <cerfcore/core/service.h>
#include "pr31x00_serial_wiring.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

class StateReader;
class StateWriter;

class Pr31x00UartModemLines : public Service {
public:
    using ControlFn = std::function<void(bool dtr, bool rts)>;

    using Service::Service;

    bool ShouldRegister() override;

    uint32_t Attach(const Pr31x00SerialModem* modem, ControlFn on_control);
    void     SetInputs(uint32_t port, bool cts, bool dsr, bool ri, bool dcd);
    void     Control(uint32_t port, bool& dtr, bool& rts) const;

    void SaveState(uint32_t port, StateWriter& w);
    void RestoreState(uint32_t port, StateReader& r);

private:
    static constexpr uint32_t kPorts = 2u;

    struct Channel {
        Pr31x00SerialModem modem;
        ControlFn          on_control;

        mutable std::mutex mu;

        uint32_t mfio_dout     = 0;
        uint32_t mfio_out_mask = 0;
        uint32_t io_dout       = 0;
        uint32_t io_out_mask   = 0;

        bool dtr = false;
        bool rts = false;
    };

    Channel&       Ch(uint32_t port);
    const Channel& Ch(uint32_t port) const;
    void           OnOut(uint32_t port, bool mfio, uint32_t dout, uint32_t out_mask);
    void           DriveInput(const Pr31x00SerialPin& p, bool asserted);
    static bool    AssertedLocked(const Channel& c, const Pr31x00SerialPin& p);

    std::array<std::unique_ptr<Channel>, kPorts> ch_;
    uint32_t                                     count_ = 0;
};
