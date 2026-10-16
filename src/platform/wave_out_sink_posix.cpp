#include "wave_out_sink.h"

#include "host_unimplemented.h"

using host_platform::NoHostImplementation;

class WaveOutSink::Impl {};

WaveOutSink::WaveOutSink() = default;
WaveOutSink::~WaveOutSink() = default;

void WaveOutSink::Start(StartHandler, CommandHandler, DoneHandler, const char*) {
    NoHostImplementation("WaveOutSink::Start");
}
void WaveOutSink::Stop() { NoHostImplementation("WaveOutSink::Stop"); }
void WaveOutSink::PostCommand(uint32_t) { NoHostImplementation("WaveOutSink::PostCommand"); }
bool WaveOutSink::EnsureFormat(uint32_t, uint16_t, uint16_t, bool, bool) {
    NoHostImplementation("WaveOutSink::EnsureFormat");
}
bool WaveOutSink::IsOpen() const { NoHostImplementation("WaveOutSink::IsOpen"); }
bool WaveOutSink::SamplePosition(uint32_t&, uint32_t&) const {
    NoHostImplementation("WaveOutSink::SamplePosition");
}
void WaveOutSink::Play(uint32_t, const uint8_t*, uint32_t) {
    NoHostImplementation("WaveOutSink::Play");
}
void WaveOutSink::Reset() { NoHostImplementation("WaveOutSink::Reset"); }
