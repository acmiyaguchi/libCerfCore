#include "omap3530_mcbsp_request_lines.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>
#include "../irq_controller.h"

namespace cerfcore {

void Omap3530McBspRequestLines::InitRequestLines(uint32_t source, Omap3530McBspDir dir, int irq) {
    cycles_  = &emu_.Get<GuestCycleClock>();
    sdma_    = &emu_.Get<Omap3530Sdma>();
    intc_    = &emu_.Get<IrqController>();
    source_  = source;
    dir_     = dir;
    irq_     = irq;
    rise_ev_ = cycles_->Add([this] { OnRise(); });
    rdy_ev_  = cycles_->Add([this] {
        const uint64_t now = cycles_->Cycles();
        Settle(now);
        UpdateIrqLine(now);
    });
    sdma_->RegisterRequester(source, this);
}

void Omap3530McBspRequestLines::SetRequestShape(bool dma_enabled, uint32_t words) {
    dma_en_      = dma_enabled;
    request_len_ = words;
}

void Omap3530McBspRequestLines::ClearRequest() {
    request_   = false;
    req_words_ = 0u;
}

void Omap3530McBspRequestLines::DropRequest() {
    request_ = false;
    line_up_ = false;
    cycles_->Disarm(rise_ev_);
}

void Omap3530McBspRequestLines::EndRequestAtThreshold() {
    if (request_ && req_words_ >= request_len_) request_ = false;
}

void Omap3530McBspRequestLines::CountRequestWord() {
    if (request_ && ++req_words_ >= request_len_) request_ = false;
}

void Omap3530McBspRequestLines::Rearm(uint64_t now) {
    if (LineAsserted()) {
        if (line_up_) {
            cycles_->Disarm(rise_ev_);
        } else {
            cycles_->Arm(rise_ev_, now);
        }
        return;
    }
    line_up_ = false;
    const uint64_t at = request_ || !dma_en_ ? kNever : ConditionCycle(now);
    if (at == kNever) {
        cycles_->Disarm(rise_ev_);
    } else {
        cycles_->Arm(rise_ev_, at);
    }
}

void Omap3530McBspRequestLines::OnRise() {
    const uint64_t now = cycles_->Cycles();
    Settle(now);
    if (!request_ && RequestCondition()) {
        request_   = true;
        req_words_ = 0u;
    }
    if (!LineAsserted() || line_up_) {
        Rearm(now);
        return;
    }
    line_up_ = true;
    sdma_->RaiseSyncEvent(source_);
}

bool Omap3530McBspRequestLines::RequestAsserted() {
    Settle(cycles_->Cycles());
    if (!request_ && RequestCondition()) {
        request_   = true;
        req_words_ = 0u;
    }
    if (LineAsserted()) line_up_ = true;
    return LineAsserted();
}

void Omap3530McBspRequestLines::Served() {
    const uint64_t now = cycles_->Cycles();
    Settle(now);
    RefreshLines(now);
}

void Omap3530McBspRequestLines::DriveIrq(bool level) {
    irq_level_ = level;
    if (level) {
        intc_->AssertIrq(irq_);
    } else {
        intc_->DeAssertIrq(irq_);
    }
}

void Omap3530McBspRequestLines::UpdateIrqLine(uint64_t now) {
    const bool level = IrqLevel();
    if (level != irq_level_) DriveIrq(level);
    const uint64_t at = level ? kNever : IrqCycle(now);
    if (at == kNever) {
        cycles_->Disarm(rdy_ev_);
    } else {
        cycles_->Arm(rdy_ev_, at);
    }
}

void Omap3530McBspRequestLines::RefreshLines(uint64_t now) {
    Rearm(now);
    UpdateIrqLine(now);
}

void Omap3530McBspRequestLines::RestoreLines() {
    const uint64_t now = cycles_->Cycles();
    cycles_->Disarm(rise_ev_);
    Rearm(now);
    DriveIrq(IrqLevel());
    UpdateIrqLine(now);
}

void Omap3530McBspRequestLines::SaveRequest(StateWriter& w) const {
    const bool rx = dir_ == Omap3530McBspDir::Rx;
    w.Write<uint8_t>(rx ? "mcbsp_rx_request" : "mcbsp_tx_request", request_ ? 1u : 0u);
    w.Write<uint32_t>(rx ? "mcbsp_rx_request_words" : "mcbsp_tx_request_words", req_words_);
    w.Write<uint8_t>(rx ? "mcbsp_rx_line_up" : "mcbsp_tx_line_up", line_up_ ? 1u : 0u);
}

void Omap3530McBspRequestLines::RestoreRequest(StateReader& r) {
    const bool rx      = dir_ == Omap3530McBspDir::Rx;
    uint8_t    request = 0, line_up = 0;
    r.Read(rx ? "mcbsp_rx_request" : "mcbsp_tx_request", request);
    r.Read(rx ? "mcbsp_rx_request_words" : "mcbsp_tx_request_words", req_words_);
    r.Read(rx ? "mcbsp_rx_line_up" : "mcbsp_tx_line_up", line_up);
    request_ = request != 0u;
    line_up_ = line_up != 0u;
}

}
