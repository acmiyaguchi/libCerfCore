#pragma once

#define NOMINMAX

#include <cerfcore/core/service.h>
#include "../host/paced_wave_out.h"
#include "freescale_sdma_bus.h"
#include "freescale_sdma_ssi_stream.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class FreescaleSsiSdmaWiring;
class StateWriter;
class StateReader;

struct FreescaleAudioFormat {
    uint32_t rate_hz  = 0;
    uint16_t channels = 0;
    uint16_t bits     = 0;
};

class FreescaleSdmaAudioPlayer : public Service, public FreescaleSdmaChannelSink {
public:
    using Service::Service;

    void OnReady() override;
    void OnShutdown() override;

    bool HandlesEvent(int event) const override;
    bool ClaimChannel(const FreescaleSdmaChannelStart& start) override;
    void ReleaseChannel(uint32_t channel) override;
    void OnSdmaClock(bool running) override;
    void SaveSinkState(StateWriter& w) override;
    void RestoreSinkState(StateReader& r) override;
    void PostRestoreSink() override;

protected:
    virtual FreescaleAudioFormat StreamFormat(uint32_t ssi) = 0;
    virtual const char*          LogTag() const = 0;

    uint32_t SampleRateHz(uint32_t ssi, uint32_t channels);

private:
    static size_t      Index(uint32_t ssi, uint32_t fifo) { return (ssi - 1u) * 2u + fifo; }
    static std::string Prefix(size_t index);

    FreescaleSdmaSsiStream& Stream(uint32_t ssi, uint32_t fifo) { return *streams_[Index(ssi, fifo)]; }

    void ResetLine();
    void OnControl();
    void OnStart(uint32_t ssi, uint32_t fifo);
    void OnHostStreamHalt();
    void ApplyFormat();
    void QueueSamples(uint32_t buf_pa, uint32_t bytes);

    FreescaleSsiSdmaWiring*                              wiring_ = nullptr;
    PacedWaveOut                                         out_;
    FreescaleAudioFormat                                 format_;
    std::vector<std::unique_ptr<FreescaleSdmaSsiStream>> streams_;
    uint32_t                                             ssi_ = 0;
};
