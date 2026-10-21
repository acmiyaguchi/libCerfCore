#pragma once

#include "../core/service.h"
#include "pointer_source.h"

namespace cerfcore {

class TouchInput : public Service, public PointerSource {
public:
    using Service::Service;
    ~TouchInput() override = default;

    virtual void OnPenDown    (int x, int y) = 0;
    virtual void OnPenMove    (int x, int y) = 0;
    virtual void OnPenUp      (int x, int y) = 0;
    virtual void OnCaptureLost()             = 0;

    /* PointerSource - the stock resistive/stylus panel. */
    std::string    SourceName()       const override { return "Stock stylus"; }
    const char*    IconResourceName() const override { return "stylus"; }
    int            SourcePriority()   const override { return 0; }
    PointerKind    Kind()             const override { return PointerKind::Stylus; }
};

}
