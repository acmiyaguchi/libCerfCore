#pragma once

#include <cerfcore/core/service.h>
#include <cerfcore/host/widget_control.h>

#include <functional>

namespace cerfcore {

class SerialModemInsertEntry : public Service {
public:
    using Service::Service;

    SlotEntry BuildInsertEntry(std::function<void()> on_insert);
};

}
