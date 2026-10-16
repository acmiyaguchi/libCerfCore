#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

struct Tps65950BootStraps {
    bool boot0 = false;
    bool boot1 = false;
};

class Tps65950Board : public Service {
public:
    using Service::Service;

    virtual uint64_t           HfclkinHz() const  = 0;
    virtual Tps65950BootStraps BootStraps() const = 0;
    virtual void               DriveInt1(bool asserted) = 0;
    virtual bool               MsecureHigh()            = 0;
};

}
