#pragma once

#include "../core/service.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace cerfcore {

class FrameSource;

class ExternalDisplay {
public:
    virtual ~ExternalDisplay() = default;

    virtual void Open(uint32_t surf_w, uint32_t surf_h) = 0;
    virtual void Close() = 0;
    virtual void SetSurfaceSize(uint32_t w, uint32_t h) = 0;
};

class ExternalDisplayFactory : public Service {
public:
    using Service::Service;

    virtual std::unique_ptr<ExternalDisplay> Create(FrameSource& source,
                                                    const std::string& title,
                                                    std::function<void()> on_eject) = 0;
};

}
