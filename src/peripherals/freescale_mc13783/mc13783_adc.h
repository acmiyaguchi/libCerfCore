#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <functional>

class StateReader;
class StateWriter;

class Mc13783Adc : public Service {
public:
    using Service::Service;

    static bool OwnsRegister(uint32_t addr) { return addr >= kRegAdc0 && addr <= kRegAdc4; }

    bool ShouldRegister() override;
    void OnReady() override;
    void Reset();

    void SetStatusSink(std::function<void(uint32_t status0)> fn) { raise_ = std::move(fn); }

    uint32_t Read(uint32_t addr, uint32_t adcsel);
    void     Write(uint32_t addr, uint32_t data, uint32_t adcsel);

    void Save(StateWriter& w);
    void Restore(StateReader& r);

private:
    static constexpr uint32_t kRegAdc0  = 43u;
    static constexpr uint32_t kRegAdc4  = 47u;
    static constexpr uint32_t kRequests = 2u;

    struct Request {
        uint32_t ctrl[3] = {};
    };

    static int      ControlIndex(uint32_t addr);
    static uint32_t TargetRequest(uint32_t data, uint32_t adcsel);
    uint32_t        ReadResult(uint32_t req);
    void            Complete(uint32_t req);

    std::function<void(uint32_t)> raise_;
    Request req_[kRequests];
};
