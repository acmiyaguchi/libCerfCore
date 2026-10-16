#pragma once

#include <cerfcore/core/service.h>
#include "../vr41xx/vr41xx_bcu_boot_write.h"

#include <vector>

class Vr4102BcuBoard : public Service {
public:
    using Service::Service;

    virtual std::vector<Vr41xxBcuBootWrite> KernelEntryWrites() const = 0;
};
