#pragma once

#include <cerfcore/core/service.h>
#include "pr31x00_sib_audio.h"

#include <cstdint>
#include <functional>

namespace cerfcore {

class StateWriter;
class StateReader;

/* Codec on the PR31x00 SIB's subframe 0. SF0AUX carries REGADDR[3:0]<30:27>,
   WRITE<26> and REGDATA[15:0]; a read's reply is latched into SF0STAT. */
class Pr31x00SibCodec : public Service {
public:
    using Service::Service;

    virtual uint16_t ReadReg(uint8_t reg) = 0;
    virtual void     WriteReg(uint8_t reg, uint16_t value) = 0;

    /* SIBIRQ input-pin level (SIB CTL $074 bit 31, §13.6.6): driven active-high
       while the codec has a pending interrupt. */
    virtual bool IrqAsserted() { return false; }

    virtual uint32_t ReceivedWord(Pr31x00SibCircuit circuit, uint32_t input_word) = 0;
    virtual bool     AudioOutputAudible() = 0;
    virtual void     RegisterBeforeChange(std::function<void()> fn) = 0;

    virtual void SaveState(StateWriter&) {}
    virtual void RestoreState(StateReader&) {}
    virtual void PostRestore() {}
};

}
