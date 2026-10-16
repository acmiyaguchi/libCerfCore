#pragma once

#include <cerfcore/core/service.h>

#include <array>
#include <cstdint>

class StateReader;
class StateWriter;

class NecMobilepro900SeriesPicLoader : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    uint8_t Enter();
    bool    Active() const { return active_; }
    bool    TakeByte(uint8_t b, uint8_t* reply);
    void    OnPicReset();

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    void StartLine();

    bool                   active_   = false;
    uint32_t               line_len_ = 0;
    uint32_t               field_    = 0;
    std::array<uint8_t, 2> type_{};
};
