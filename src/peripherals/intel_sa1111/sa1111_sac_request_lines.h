#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"

#include <cstdint>
#include <functional>

namespace cerfcore {

class Sa1111Intc;
class Sa1111SacFrameClock;
class Sa1111SacL3;
class Sa1111SacRxFifo;
class Sa1111SacTxStream;

class Sa1111SacRequestLines : public Service {
public:
    using Service::Service;

    /* SA-1111 Developer's Manual Table 11-1: 36 AudTFSR, 37 AudRFSR, 38 AudTUR, 39 AudROR,
       40 AudDTS. */
    static constexpr uint8_t  kSrcTfsr = 36u;
    static constexpr uint8_t  kSrcRfsr = 37u;
    static constexpr uint8_t  kSrcTur  = 38u;
    static constexpr uint8_t  kSrcRor  = 39u;
    static constexpr uint8_t  kSrcDts  = 40u;
    /* Table 11-1 (page 11-3): 41 AudRDD "Audio L3/ACLink Data Read Done interrupt". */
    static constexpr uint8_t  kSrcRdd  = 41u;
    static constexpr uint32_t kSampledSources1 = 0x0Fu << (kSrcTfsr - 32u);

    /* Transmit Done A = source 32, Done B = source 34 (Table 11-1). */
    static constexpr uint8_t kSrcDoneA = 32u;
    static constexpr uint8_t kSrcDoneB = 34u;
    /* Table 11-1: 33 AudRcvDmaDoneA, 35 AudRcvDmaDoneB. */
    static constexpr uint8_t kSrcRxDoneA = 33u;
    static constexpr uint8_t kSrcRxDoneB = 35u;

    struct Inputs {
        bool     enabled         = false;
        uint32_t tx_threshold    = 0;
        uint32_t rx_threshold    = 0;
        uint32_t tx_words_left   = 0;
        bool     tx_draining     = false;
        uint64_t tx_next_request = 0;
        uint64_t rx_requests     = 0;
        bool     rx_draining     = false;
        uint64_t rx_next_request = 0;
    };

    bool ShouldRegister() override;
    void OnReady() override;

    void StrobeDone(uint8_t source);
    void Publish(uint64_t now, const Inputs& in);
    void Baseline(uint64_t now, const Inputs& in);
    void SetEdgeHandler(std::function<void(uint64_t now)> on_edge);
    void RefreshEdges(uint64_t now, const Inputs& in);

private:
    struct Levels {
        bool     tfs         = false;
        uint64_t requests    = 0;
        bool     rfs         = false;
        uint64_t rx_requests = 0;
        bool     tur         = false;
        bool     ror         = false;
        bool     dts         = false;
        bool     rdd         = false;
    };

    Levels Sample(uint64_t now, const Inputs& in) const;
    void   PulseRequests(uint8_t source, uint64_t requests, bool& published);

    bool NextEdge(uint64_t now, const Inputs& in, uint64_t& cycle);

    Sa1111Intc*        intc_   = nullptr;
    Sa1111SacTxStream* stream_ = nullptr;
    Sa1111SacFrameClock* frames_ = nullptr;
    Sa1111SacRxFifo*   rx_     = nullptr;
    Sa1111SacL3*       l3_     = nullptr;
    GuestCycleClock*   clock_  = nullptr;
    GuestCycleClock::Event* edge_ev_ = nullptr;
    std::function<void(uint64_t now)> on_edge_;
    Levels             pub_;
};

}
