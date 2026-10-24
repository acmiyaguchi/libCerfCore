#pragma once

#include "imx28_apb_dma_device.h"
#include "imx28_apb_dma_spec.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

class CerfEmulator;
class StateReader;
class StateWriter;

/* MCIMX28RM Rev. 2 sections 6.2 and 7.2: what a channel needs from its bridge. */
class Imx28ApbDmaArbiter {
public:
    virtual ~Imx28ApbDmaArbiter() = default;

    virtual bool ChannelGranted(uint32_t channel) const = 0;
    virtual void CommandComplete(uint32_t channel) = 0;
    virtual void DeviceTerminated(uint32_t channel) = 0;
};

/* MCIMX28RM Rev. 2 sections 6.2 and 7.2: one DMA channel and its command chain. */
class Imx28ApbDmaChannel {
public:
    Imx28ApbDmaChannel(CerfEmulator& emu, const Imx28ApbDmaSpec& spec, uint32_t mmio_base,
                       uint32_t index, Imx28ApbDmaArbiter& arbiter);

    void Attach(Imx28ApbDmaDevice* device);
    void Reset();
    void SetNextCommand(uint32_t address) { nxt_ = address; }
    void IncrementSemaphore(uint32_t count);
    void Resume();
    void EndCommand();
    void TransferDone();

    uint32_t Current() const { return cur_; }
    uint32_t Next() const { return nxt_; }
    uint32_t Command() const { return cmd_; }
    uint32_t Buffer() const { return bar_; }
    uint32_t Phore() const { return sema_; }
    uint32_t Debug1() const;

    void Save(StateWriter& w) const;
    void Restore(StateReader& r);

private:
    enum class State : uint8_t {
        Idle, Pending, WaitEnd, HaltAfterTerminate, ReadRequest, CheckWait, WriteRequest
    };

    struct Descriptor {
        uint32_t cmd;
        uint32_t bar;
        uint32_t pio[15];
    };

    [[noreturn]] void Die(const char* what, uint32_t value) const;
    void     Run();
    bool     Fetch(Descriptor& d);
    bool     Execute(const Descriptor& d);
    static uint32_t TransferCount(uint32_t cmd);
    bool     MoveData(const Descriptor& d);
    bool     Terminated(uint32_t cmd);
    bool     Complete(uint32_t cmd);
    void     ReadGuest(uint32_t pa, void* dst, uint32_t bytes) const;
    void     WriteGuest(uint32_t pa, const void* src, uint32_t bytes) const;

    CerfEmulator&          emu_;
    const Imx28ApbDmaSpec& spec_;
    const uint32_t         mmio_base_;
    const uint32_t         index_;
    Imx28ApbDmaArbiter&    arbiter_;
    Imx28ApbDmaDevice*     device_ = nullptr;
    std::vector<uint32_t>  visited_;

    uint32_t cur_  = 0u;
    uint32_t nxt_  = 0u;
    uint32_t cmd_  = 0u;
    uint32_t bar_  = 0u;
    uint32_t sema_ = 0u;
    State    state_ = State::Idle;
};

}
