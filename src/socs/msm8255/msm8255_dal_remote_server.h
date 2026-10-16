#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

class StateReader;
class StateWriter;

class Msm8255DalRemoteServer : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t Answer(uint32_t in_pa, uint32_t in_avail, uint32_t out_pa,
                    uint32_t out_cap, uint32_t& consumed);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    bool port_announced_ = false;
};
