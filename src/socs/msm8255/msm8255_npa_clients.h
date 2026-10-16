#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <vector>

class StateReader;
class StateWriter;

class Msm8255NpaClients : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t ResourceKeyAt(uint32_t body, uint32_t off);
    uint32_t IssueHandle(uint32_t resource);
    uint32_t HandleCount() const;
    uint32_t ApplyRequest(uint32_t handle, uint32_t request);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    std::vector<uint32_t> resource_;
    std::vector<uint32_t> request_;
    std::vector<uint32_t> issued_;
};
