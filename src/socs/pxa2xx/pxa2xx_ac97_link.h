#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../peripherals/ac97_codec.h"
#include "../rated_tick_count.h"

#include <cstdint>
#include <vector>

class Pxa2xxAc97BitClock;
class StateReader;
class StateWriter;

class Pxa2xxAc97LinkListener {
public:
    virtual ~Pxa2xxAc97LinkListener() = default;
    virtual void OnLinkRun(uint64_t cycle)    = 0;
    virtual void OnLinkStop(uint64_t cycle)   = 0;
    virtual void OnFifoReset(uint64_t cycle, bool cold) = 0;
    virtual void OnCodecWrite(uint64_t cycle) = 0;
    virtual void OnLinkEvent()                = 0;
};

class Pxa2xxAc97Link : public Service, public Ac97FrameSource {
public:
    using Service::Service;

    static constexpr uint64_t kNever = UINT64_MAX;

    bool ShouldRegister() override;
    void OnReady() override;

    void AddListener(Pxa2xxAc97LinkListener* listener) { listeners_.push_back(listener); }

    bool FrameAt(uint64_t cycle, uint64_t& frame) const override;
    void OnCodecStreamChange() override;
    void CodecWake(uint64_t cycle, bool codec_restarts) override;
    void CodecGpioInterrupt(uint64_t frame, bool level) override;

    void     Settle(uint64_t now);
    bool     Running() const { return running_; }
    bool     RequestsEnabled() const { return cold_released_ && !requests_off_; }
    bool     DmaRequestsEnabled() const { return RequestsEnabled() && !dma_masked_; }
    void     SetDmaRequestsMasked(bool masked) { dma_masked_ = masked; }
    /* AC '97 Component Specification Revision 2.1 Figure 13 (page 32): BIT_CLK 12.288 MHz, a
       20.8 us (48 kHz) frame; Intel PXA27x Developer's Manual section 13.6.4 (page 13-18): "The
       AC '97 controller divides the AC97_BITCLK by 256 to generate the AC97_SYNC signal". */
    uint64_t FrameIndexAt(uint64_t cycle) const { return bits_.TicksAt(cycle) / Ac97Codec::kBitsPerFrame; }
    uint64_t CycleOfFrame(uint64_t frame) const { return bits_.CycleOfTick(frame * Ac97Codec::kBitsPerFrame); }

    void SetColdReset(uint64_t now, bool asserted);
    void SetCodecResetPin(uint64_t now, bool asserted);
    void SetLinkOff(uint64_t now, bool off, bool discard_fifos);
    bool WarmReset(uint64_t now);

    static bool InCodecWindow(uint32_t off) { return off >= kCodecBase && off < kCodecEnd; }
    uint32_t    CodecWindowRead(uint64_t now, uint32_t off);
    void        CodecWindowWrite(uint64_t now, uint32_t off, uint16_t value);
    bool        ReadCar(uint64_t now);
    void        WriteCar(uint64_t now, bool caip);

    void SetReadTimeoutValue(uint32_t value) { timeout_value_ = value; }
    void RequireCodec(const char* fifo, const char* access);

    bool CommandDone(uint64_t now);
    bool StatusDone(uint64_t now);
    bool ReadTimedOut(uint64_t now);
    bool CodecReady(uint64_t now);
    bool ResumeEvent(uint64_t now);
    bool GpioStatus(uint64_t now);
    void ClearDone(uint64_t now, bool command, bool status, bool timeout, bool resume, bool gpio);
    uint64_t NextGpioStatusCycle(uint64_t now);

    void ResetLine();
    void Save(StateWriter& w);
    void Restore(StateReader& r);

private:
    enum class Cmd : uint8_t { None = 0, Write = 1, Read = 2 };

    static constexpr uint32_t kCodecBase   = 0x200u;
    static constexpr uint32_t kCodecEnd    = 0x600u;
    static constexpr uint32_t kModemWindow = 0x200u;

    static bool SecondaryWindow(uint32_t off);
    uint32_t CodecRead(uint64_t now, uint32_t reg, bool absent);
    void     CodecWrite(uint64_t now, uint32_t reg, uint16_t value, bool absent);

    void UpdateRun(uint64_t now);
    void AssertColdReset(uint64_t now);
    void OnBitClock(uint64_t at);
    void SetBitRate(uint64_t now, bool rescale);
    void CompleteCommand(uint64_t at);
    void OnCpuRate();
    void NotifyEvent();

    GuestCycleClock*        clock_     = nullptr;
    GuestCycleClock::Event* notify_ev_ = nullptr;
    Ac97Codec*              codec_     = nullptr;
    Pxa2xxAc97BitClock*     bit_clock_ = nullptr;
    std::vector<Pxa2xxAc97LinkListener*> listeners_;
    RatedTickCount          bits_;
    GuestCycleClock::Rate   bits_rate_;

    bool     cold_released_ = false;
    bool     reset_pin_     = false;
    bool     off_           = false;
    bool     requests_off_  = false;
    bool     dma_masked_    = false;
    bool     running_       = false;
    bool     stopping_      = false;
    bool     ready_         = false;
    Cmd      cmd_           = Cmd::None;
    bool     cmd_absent_    = false;
    uint32_t cmd_reg_       = 0;
    uint16_t cmd_value_     = 0;
    uint32_t latch_         = 0;
    uint32_t timeout_value_ = 0;
    bool     cdone_         = false;
    bool     sdone_         = false;
    bool     rdcs_          = false;
    bool     caip_          = false;
    bool     resume_        = false;
    bool     gpi_           = false;
    bool     gsci_          = false;
    uint64_t gpi_frame_     = 0;
    uint16_t gpio_shadow_   = 0;
};
