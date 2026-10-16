#pragma once

#include <cerfcore/core/service.h>

#include <atomic>

namespace cerfcore {

class StateWriter;
class StateReader;

class CerfGuestLiveness : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool IsAlive() const { return alive_.load(std::memory_order_acquire); }

    void NotifyBodyFetch();

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    void Clear();

    std::atomic<bool> alive_{false};
};

}
