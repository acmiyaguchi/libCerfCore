#define NOMINMAX

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/core/service.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/state/state_stream.h>
#include "freescale_module_clocks.h"
#include "freescale_sdma_bus.h"
#include "freescale_sdma_ssi_stream.h"
#include "freescale_ssi_core.h"
#include "freescale_ssi_sdma_wiring.h"
#include <cerfcore/socs/guest_cpu_reset.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

class FreescaleSdmaSsiReceive : public Service, public FreescaleSdmaChannelSink {
public:
    using Service::Service;

    bool ShouldRegister() override { return emu_.TryGet<FreescaleSsiSdmaWiring>() != nullptr; }

    void OnReady() override {
        wiring_ = &emu_.Get<FreescaleSsiSdmaWiring>();
        for (uint32_t i = 0; i < wiring_->SsiCount() * 2u; ++i) {
            streams_.push_back(std::make_unique<FreescaleSdmaSsiStream>(emu_));
            FreescaleSdmaSsiStream::Hooks hooks;
            hooks.retire = [this](uint32_t buffer, uint32_t bytes) { Fill(buffer, bytes); };
            streams_.back()->Attach(wiring_->Sdma(), "SSI-Rx", std::move(hooks));
        }
        for (uint32_t ssi = 1u; ssi <= wiring_->SsiCount(); ++ssi) {
            wiring_->Core(ssi).RegisterControlListener([this] { Rearm(); });
        }
        emu_.Get<FreescaleModuleClocks>().RegisterGateListener([this] { Rearm(); });
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { ResetLine(); });
        wiring_->Sdma().RegisterChannelSink(this);
    }

    bool HandlesEvent(int event) const override {
        uint32_t ssi = 0, fifo = 0;
        return wiring_->RxEventFifo(event, ssi, fifo);
    }

    bool ClaimChannel(const FreescaleSdmaChannelStart& start) override {
        uint32_t ssi = 0, fifo = 0;
        if (!wiring_->RxEventFifo(start.event, ssi, fifo)) return false;
        const uint32_t bytes = wiring_->RxSampleBytes(ssi);
        FreescaleSdmaSsiStream& s = *streams_[Index(ssi, fifo)];
        s.Claim(start, wiring_->Core(ssi).RxPort(fifo), bytes);
        LOG(Periph, "[SSI-Rx] claim SSI%u receive FIFO %u (ch%u ev=%d, %u-byte words, %u-word "
                    "bursts)\n",
            ssi, fifo, start.channel, start.event, bytes, s.BurstWords());
        return true;
    }

    void ReleaseChannel(uint32_t channel) override {
        for (auto& s : streams_) {
            if (s->Release(channel)) return;
        }
    }

    void OnSdmaClock(bool running) override {
        for (auto& s : streams_) s->SetSdmaClock(running);
    }

    void SaveSinkState(StateWriter& w) override {
        for (size_t i = 0; i < streams_.size(); ++i) streams_[i]->Save(w, Prefix(i).c_str());
    }

    void RestoreSinkState(StateReader& r) override {
        for (size_t i = 0; i < streams_.size(); ++i) {
            const uint32_t ssi  = static_cast<uint32_t>(i / 2u) + 1u;
            const uint32_t fifo = static_cast<uint32_t>(i % 2u);
            streams_[i]->Restore(r, Prefix(i).c_str(), &wiring_->Core(ssi).RxPort(fifo));
        }
    }

    void PostRestoreSink() override {
        for (auto& s : streams_) s->PostRestore();
    }

private:
    static size_t Index(uint32_t ssi, uint32_t fifo) { return (ssi - 1u) * 2u + fifo; }

    static std::string Prefix(size_t i) {
        return "ssi_rx" + std::to_string(i / 2u + 1u) + "_" + std::to_string(i % 2u) + "_";
    }

    void Rearm() {
        for (auto& s : streams_) s->Rearm();
    }

    void ResetLine() {
        for (auto& s : streams_) s->Reset();
    }

    void Fill(uint32_t buffer, uint32_t bytes) {
        const uint8_t zeros[256] = {};
        auto&         mem        = emu_.Get<EmulatedMemory>();
        for (uint32_t off = 0; off < bytes;) {
            const uint32_t n = std::min<uint32_t>(bytes - off, sizeof(zeros));
            if (mem.TryCopyIn(buffer + off, zeros, n) != n) return;
            off += n;
        }
    }

    FreescaleSsiSdmaWiring*                              wiring_ = nullptr;
    std::vector<std::unique_ptr<FreescaleSdmaSsiStream>> streams_;
};

}

REGISTER_SERVICE(FreescaleSdmaSsiReceive);
