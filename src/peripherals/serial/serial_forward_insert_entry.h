#pragma once

#include <cerfcore/core/service.h>
#include <cerfcore/host/widget_control.h>

#include <functional>
#include <string>

namespace cerfcore {

class SerialForwardInsertEntry : public Service {
public:
    using Service::Service;

    SlotEntry BuildInsertEntry(std::function<void(std::string host_port)> on_insert);
};

}
