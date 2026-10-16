#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <mutex>

namespace cerfengine { class HostRequestChannel; }

namespace cerfcore {

class Vr41xxKiuKeyPins : public Service {
public:
    using Service::Service;

    static constexpr uint32_t kRegs = 6u;

    bool ShouldRegister() override;
    void OnReady() override;

    void Set(uint8_t matrix_index, bool pressed);
    bool Take(uint16_t (&pins)[kRegs]);
    void Clear();

private:
    std::mutex          mtx_;
    uint16_t            held_[kRegs] = {0, 0, 0, 0, 0, 0};
    bool                dirty_ = false;
    cerfengine::HostRequestChannel* host_requests_ = nullptr;
};

}
