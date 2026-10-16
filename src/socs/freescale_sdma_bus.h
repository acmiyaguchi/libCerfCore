#pragma once

#include <cstddef>
#include <cstdint>

class StateWriter;
class StateReader;

struct FreescaleSdmaChannelStart {
    uint32_t channel;
    int      event;
    uint32_t base_bd_pa;
    uint32_t stride;
    uint32_t ccb_pa;
};

class FreescaleSdmaChannelSink {
public:
    virtual ~FreescaleSdmaChannelSink() = default;

    virtual bool HandlesEvent(int event) const = 0;
    virtual bool ClaimChannel(const FreescaleSdmaChannelStart& start) = 0;
    virtual void ReleaseChannel(uint32_t channel) = 0;
    virtual void OnSdmaClock(bool running) = 0;
    virtual void SaveSinkState(StateWriter& w) = 0;
    virtual void RestoreSinkState(StateReader& r) = 0;
    virtual void PostRestoreSink() = 0;
};

class FreescaleSdmaChannelHost {
public:
    virtual ~FreescaleSdmaChannelHost() = default;

    virtual void RegisterChannelSink(FreescaleSdmaChannelSink* sink) = 0;
    virtual void SignalChannelBdDone(uint32_t channel, uint32_t bd_pa) = 0;
    virtual void StopChannel(uint32_t channel) = 0;
    virtual bool EndChannelRun(uint32_t channel) = 0;
    virtual bool ChannelWatermark(uint32_t channel, uint32_t& value) const = 0;
};
