#pragma once

#include "../core/service.h"

#include <cstdint>
#include <functional>
#include <string>

enum class AlertLevel : uint8_t { kError, kWarning };

enum class AlertPlacement : uint8_t { kStandalone, kTopmost, kOverWindow };

class UserAlert : public Service {
public:
    using Service::Service;

    virtual void Show(AlertLevel level, AlertPlacement placement, const std::string& title,
                      const std::string& text) = 0;
    virtual void Post(AlertLevel level, const std::string& title, const std::string& text,
                      std::function<void()> after) = 0;
};
