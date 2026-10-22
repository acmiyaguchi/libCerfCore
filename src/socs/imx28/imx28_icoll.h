#pragma once

#include "imx28_apb_peripheral.h"

#include <cerfcore/state/state_stream.h>

#include <cstdint>
#include <mutex>

namespace cerfengine { class ArmJit; }

namespace cerfcore {

/* MCIMX28RM Rev. 2 chapter 5 (ICOLL) at 0x8000_0000. */
class Imx28Icoll : public Imx28ApbPeripheral {
public:
    using Imx28ApbPeripheral::Imx28ApbPeripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x80000000u; }
    uint32_t MmioSize() const override { return 0x00002000u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    /* Table 5-1 source number 0-127. */
    void AssertSource(uint32_t source);
    void DeassertSource(uint32_t source);

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

protected:
    uint32_t PeekLanes(uint32_t word_addr, uint32_t lanes) override;

private:
    static constexpr uint32_t kSources = 128u;
    static constexpr uint32_t kWords   = kSources / 32u;

    bool     Operating() const { return !Gated(ctrl_); }
    bool     Requested(uint32_t n) const;
    bool     Candidate(uint32_t n, bool fiq) const;
    uint32_t CandidateWord(uint32_t word, bool fiq) const;
    bool     AnyEnabledRequest() const;
    uint32_t Pitch() const;
    uint32_t ComposeRead(uint32_t index, uint32_t addr);
    void     Evaluate();
    void     Notify();
    void     Reset();
    void     EnterService();
    void     AcknowledgeLevel(uint32_t value);
    void     WriteCtrl(Imx28SctOp op, uint32_t value);
    void     WriteInterrupt(uint32_t n, Imx28SctOp op, uint32_t value);
    uint32_t ReadLocked(uint32_t addr);
    void     WriteLocked(uint32_t addr, uint32_t value);

    mutable std::mutex      state_mtx_;
    cerfengine::ArmJit*     jit_ = nullptr;

    uint32_t raw_[kWords]    = {};
    uint8_t  interrupt_[kSources] = {};
    uint32_t ctrl_           = 0u;
    uint32_t vbase_          = 0u;
    uint32_t dbgflag_        = 0u;
    uint32_t inservice_      = 0u;
    uint32_t held_vector_    = 0u;
    uint32_t held_number_    = 0u;
    uint32_t presented_level_ = 0u;
    bool     pending_        = false;
    bool     stat_valid_     = false;
};

}
