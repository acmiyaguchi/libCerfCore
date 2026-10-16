#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"
#include "omap3530_mcbsp_frame.h"
#include "omap3530_sdma.h"

#include <cstdint>

class IrqController;
class StateReader;
class StateWriter;

class Omap3530McBspRequestLines : public Service, public Omap3530SdmaRequester {
public:
    using Service::Service;

    virtual void Settle(uint64_t now) = 0;

    bool RequestAsserted() override;
    void Served() override;

protected:
    static constexpr uint64_t kNever = UINT64_MAX;

    void InitRequestLines(uint32_t source, Omap3530McBspDir dir, int irq);
    void SetRequestShape(bool dma_enabled, uint32_t words);
    void ClearRequest();
    void DropRequest();
    void EndRequestAtThreshold();
    void CountRequestWord();
    void Rearm(uint64_t now);
    void SaveRequest(StateWriter& w) const;
    void RestoreRequest(StateReader& r);
    uint32_t         RequestWords() const { return request_len_; }
    Omap3530McBspDir Direction() const { return dir_; }

    void UpdateIrqLine(uint64_t now);
    void RefreshLines(uint64_t now);
    void RestoreLines();

    virtual bool     RequestCondition() const               = 0;
    virtual uint64_t ConditionCycle(uint64_t now) const     = 0;
    virtual bool     IrqLevel() const                       = 0;
    virtual uint64_t IrqCycle(uint64_t now) const           = 0;

    GuestCycleClock* cycles_ = nullptr;

private:
    bool LineAsserted() const { return request_ && dma_en_; }
    void OnRise();
    void DriveIrq(bool level);

    Omap3530Sdma*           sdma_        = nullptr;
    GuestCycleClock::Event* rise_ev_     = nullptr;
    uint32_t                source_      = 0;
    Omap3530McBspDir        dir_         = Omap3530McBspDir::Tx;
    bool                    dma_en_      = false;
    uint32_t                request_len_ = 1u;
    bool                    request_     = false;
    uint32_t                req_words_   = 0u;
    bool                    line_up_     = false;

    IrqController*          intc_      = nullptr;
    GuestCycleClock::Event* rdy_ev_    = nullptr;
    int                     irq_       = 0;
    bool                    irq_level_ = false;
};
