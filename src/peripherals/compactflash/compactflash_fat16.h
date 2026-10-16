#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <string>
#include <vector>

namespace cerfcore {

class CompactFlashFat16Builder : public Service {
public:
    using Service::Service;

    bool Build(const std::string& out_path,
               const std::vector<std::string>& files,
               uint32_t data_mb);
};

}
