#pragma once

#include <cerfcore/core/service.h>
#include <cerfengine/arm/arm_instruction_timing.h>

namespace cerfcore {

class ArmInstructionTimingModel : public Service, public cerfengine::ArmInstructionTiming {
public:
    using Service::Service;
};

}
