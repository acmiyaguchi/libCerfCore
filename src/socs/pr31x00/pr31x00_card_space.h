#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class PcmciaSlot;
class StateReader;
class StateWriter;

/* A board may interpose a PC Card buffer chip between the SoC's Card window and
   the socket. Implemented by that chip; a board with none leaves it absent and
   the window decodes per §4.2.1 alone. */
class Pr31x00CardBuffer {
public:
    virtual ~Pr31x00CardBuffer() = default;

    /* False while the buffer's card drivers are off: the socket sees no cycle. */
    virtual bool CardInterfaceEnabled() const = 0;

    /* True while the buffer derives attribute-vs-I/O from address bit 25 instead
       of the BIU's CARDnIOEN. */
    virtual bool FixedAttributeIo() const = 0;
};

/* Philips PR31x00 PC Card window decode. Table 4.2.1 places Card 1 and Card 2
   I/O-or-Attribute space at PA $0800_0000 and $0C00_0000 and their common memory
   at PA $6400_0000 and $6800_0000, 64 MB each. */
class Pr31x00CardSpace : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    /* Called from the board socket controller's OnReady. */
    void ProvideSockets(PcmciaSlot* socket0, PcmciaSlot* socket1);
    PcmciaSlot* Socket(int n) const;

    /* Called from the buffer chip's OnReady, when the board has one. */
    void ProvideCardBuffer(Pr31x00CardBuffer* buffer);

    /* PA $0800_0000 window, offset from its base. */
    uint8_t  ReadCtrl8  (uint32_t off);
    uint16_t ReadCtrl16 (uint32_t off);
    void     WriteCtrl8 (uint32_t off, uint8_t  value);
    void     WriteCtrl16(uint32_t off, uint16_t value);

    /* PA $6400_0000 window, offset from its base. */
    uint8_t  ReadMem8  (uint32_t off);
    uint16_t ReadMem16 (uint32_t off);
    void     WriteMem8 (uint32_t off, uint8_t  value);
    void     WriteMem16(uint32_t off, uint16_t value);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);
    void PostRestore();

private:
    PcmciaSlot* DecodeCtrl(uint32_t off, bool* io_space, uint32_t* card_offset) const;
    PcmciaSlot* DecodeMem (uint32_t off, uint32_t* card_offset) const;

    PcmciaSlot*        sockets_[2] = {};
    Pr31x00CardBuffer* buffer_     = nullptr;
};

}
