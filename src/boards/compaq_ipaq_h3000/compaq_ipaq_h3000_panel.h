#pragma once

#include <cerfcore/core/service.h>

namespace cerfcore {

class CompaqIpaqH3000Panel : public Service {
public:
    using Service::Service;

    struct Kind {
        bool color;
        bool rotate_cw;
    };

    virtual Kind PanelKind() const = 0;
};

}
