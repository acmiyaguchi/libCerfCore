#include "wave_in_fifo.h"

#include "host_unimplemented.h"

namespace cerfcore {

using host_platform::NoHostImplementation;

class WaveInFifo::Impl {};

WaveInFifo::WaveInFifo() = default;
WaveInFifo::~WaveInFifo() = default;

void WaveInFifo::Start(const char*, size_t) { NoHostImplementation("WaveInFifo::Start"); }
void WaveInFifo::Stop() { NoHostImplementation("WaveInFifo::Stop"); }
void WaveInFifo::Request(uint32_t, uint16_t, uint16_t) {
    NoHostImplementation("WaveInFifo::Request");
}
void WaveInFifo::Clear() { NoHostImplementation("WaveInFifo::Clear"); }
size_t WaveInFifo::Take(uint8_t*, size_t) { NoHostImplementation("WaveInFifo::Take"); }

}
