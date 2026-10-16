#pragma once

#include <cerfcore/core/service.h>

#include <cstddef>
#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;
class Twl4030;

class Tps65950PowerMaster : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool ConsumeWrite(uint8_t sub, uint8_t& value);
    bool ConsumeRead(uint8_t sub, uint8_t& value);

    void Save(StateWriter& w);
    void Restore(StateReader& r);

private:
    void    WriteMemoryData(uint8_t value);
    uint8_t ReadMemoryData();
    void    WriteProtectKey(uint8_t value);
    uint8_t LockedBits(uint8_t sub) const;
    void    StoreKeyRead();
    void    OnReset(bool power_on);
    void    PowerOn();
    void    ResetPowerBits();
    bool    RunWarmSequence();
    bool    InGroups(uint8_t dev_grp_reg, uint8_t groups) const;
    void    WarmResetRail(size_t rail);
    uint8_t NextAddress();

    Twl4030* twl_         = nullptr;
    uint8_t  memory_[256] = {};
    uint8_t  known_[256]  = {};
    bool     key_cfg_     = false;
    bool     key_tst_     = false;
    uint8_t  key_armed_   = 0;
};

}
