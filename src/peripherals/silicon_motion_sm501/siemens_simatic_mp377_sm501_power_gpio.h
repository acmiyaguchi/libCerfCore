#pragma once

#include "../../core/deferred_service.h"
#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

namespace siemens_simatic_mp377 {

class SiemensSimaticMp377Sm501Ac97;
class SiemensSimaticMp377Sm501Regs;

// SM501 Databook ch. 6 and ch. 17: power-mode gate/clock selection and GPIO
// pin multiplexing, including the MP377 board's CS4297A reset wiring.
class SiemensSimaticMp377Sm501PowerGpio : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void Initialize(SiemensSimaticMp377Sm501Regs& registers);
    uint32_t CurrentGate() const;
    uint32_t CurrentClock() const;
    bool IsGateEnabled(uint32_t gate_bit) const;
    void WritePowerModeControl(uint32_t value);
    bool IsAc97LinkMuxed() const;
    bool IsCodecResetDeasserted() const;
    uint32_t ReadGpioDataLow() const;
    uint32_t ReadGpioDirectionLow() const;
    void UpdateAc97Link();

private:
    uint32_t CurrentGateMask() const;
    uint32_t ActiveAc97GpioMuxMask() const;

    DeferredService<SiemensSimaticMp377Sm501Regs> registers_{emu_};
    DeferredService<SiemensSimaticMp377Sm501Ac97> ac97_{emu_};
};

} // namespace siemens_mp377

}
