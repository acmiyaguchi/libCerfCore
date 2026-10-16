#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class StateWriter;
class StateReader;

/* Codec on the SA-11x0 MCP: the MCP decodes an MCDR2 register command and
   routes the access here. The board's concrete registers via
   REGISTER_SERVICE_AS, parallel to Sa11xxSspDevice; with no codec the MCP
   reads back 0. */
class Sa11xxMcpCodec : public Service {
public:
    using Service::Service;

    virtual uint16_t ReadReg(uint8_t reg) = 0;
    virtual void     WriteReg(uint8_t reg, uint16_t value) = 0;

    /* The owning MCP forwards its snapshot here so the codec's guest-written
       registers survive a hibernate. */
    virtual void SaveState(StateWriter&) {}
    virtual void RestoreState(StateReader&) {}
    virtual void PostRestore() {}
};

}
