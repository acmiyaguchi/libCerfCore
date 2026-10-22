#pragma once

#include <cerfcore/core/service.h>

#include <functional>
#include <vector>

namespace cerfcore {

class StateBootGate : public Service {
public:
    using Service::Service;
    void OnReady() override;

    void RegisterColdHandoff(std::function<void()> fn);

private:
    void Run();

    std::vector<std::function<void()>> cold_handoffs_;
};

}
