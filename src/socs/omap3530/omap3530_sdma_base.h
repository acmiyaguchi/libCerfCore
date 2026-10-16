#pragma once

#include <cerfcore/state/state_stream.h>
#include "omap3530_sdma_irq.h"

#include <cstdint>
#include <mutex>
#include <vector>

class Omap3530DmaElementBus;

class Omap3530SdmaBase : public Omap3530SdmaIrq {
public:
    using Omap3530SdmaIrq::Omap3530SdmaIrq;

    void OnReady() override;

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void RaiseSyncEvent(uint32_t source);

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override {
        std::lock_guard<std::recursive_mutex> lk(state_mu_);
        RestoreIrqRegisters(r);
        r.Read("sysstatus", sysstatus_);
        r.Read("ocp_sysconfig", ocp_sysconfig_);
        r.Read("gcr", gcr_);
        StateReadField field(r);
        for (Channel& c : channels_) VisitChannel(c, field);
        RestoreIrqLines(r);
    }

protected:
    virtual uint32_t ChannelCount() const = 0;
    virtual bool     SyncRequestAsserted(uint32_t) { return false; }
    virtual void     OnSyncServed(uint32_t) {}

private:
    struct Channel {
        uint32_t ccr       = 0;
        uint32_t clnk_ctrl = 0;
        uint32_t cicr      = 0;
        uint32_t csr       = 0;
        uint32_t csdp      = 0;
        uint32_t cen       = 0;
        uint32_t cfn       = 0;
        uint32_t cssa      = 0;
        uint32_t cdsa      = 0;
        uint32_t csei      = 0;
        uint32_t csfi      = 0;
        uint32_t cdei      = 0;
        uint32_t cdfi      = 0;
        uint32_t csac      = 0;
        uint32_t cdac      = 0;
        uint32_t ccen      = 0;
        uint32_t ccfn      = 0;
        uint32_t color     = 0;
        bool     active    = false;
        bool     stopped   = false;
        uint8_t  pad[2]    = {};
    };

    template <typename F>
    static constexpr void VisitChannel(Channel& c, F& field) {
        field("ccr", c.ccr);
        field("clnk_ctrl", c.clnk_ctrl);
        field("cicr", c.cicr);
        field("csr", c.csr);
        field("csdp", c.csdp);
        field("cen", c.cen);
        field("cfn", c.cfn);
        field("cssa", c.cssa);
        field("cdsa", c.cdsa);
        field("csei", c.csei);
        field("csfi", c.csfi);
        field("cdei", c.cdei);
        field("cdfi", c.cdfi);
        field("csac", c.csac);
        field("cdac", c.cdac);
        field("ccen", c.ccen);
        field("ccfn", c.ccfn);
        field("color", c.color);
        field("active", c.active);
        field("stopped", c.stopped);
        field.Skip(c.pad);
    }

    static void SetStatus(Channel& c, uint32_t events);
    void ResetLocked(bool power_on);
    void RunSwTransfer(int ch);
    uint32_t ExecuteSyncUnit(int ch);
    bool TransferOneElement(int ch);
    uint32_t StepAddress(uint32_t cur, uint32_t amode,
                         int32_t ei, int32_t fi,
                         uint32_t es, bool end_of_frame) const;
    void RaiseChannelFault(int ch, uint32_t csr_fault_bits);
    void OnChannelComplete(int ch);
    void StartChain(int ch);
    void ServeIfRequested(int ch);
    void ServeChainStarts();
    uint32_t FiredChannels() const override;

    Omap3530DmaElementBus* bus_ = nullptr;

    uint32_t sysstatus_        = 0;
    uint32_t ocp_sysconfig_    = 0;
    uint32_t gcr_              = 0;
    std::vector<Channel> channels_;
    std::vector<int>     chain_starts_;

    mutable std::recursive_mutex state_mu_;
};

inline void Omap3530SdmaBase::SaveState(StateWriter& w) {
    std::lock_guard<std::recursive_mutex> lk(state_mu_);
    SaveIrqRegisters(w);
    w.Write("sysstatus", sysstatus_);
    w.Write("ocp_sysconfig", ocp_sysconfig_);
    w.Write("gcr", gcr_);
    static_assert(StateVisitCoversAllBytes<Channel>(
                      [](Channel& c, StateFieldBytes& f) { VisitChannel(c, f); }),
                  "VisitChannel must name or skip every field of Channel");
    StateWriteField field(w);
    for (Channel& c : channels_) VisitChannel(c, field);
    SaveIrqLines(w);
}
