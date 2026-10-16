#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

struct Msm8255RamPartition {
    uint32_t start;
    uint32_t size;
    uint32_t attr;
    uint32_t category;
    uint32_t domain;
    uint32_t type;
};

class Msm8255RamPartitions : public Service {
public:
    using Service::Service;

    virtual uint32_t PartitionCount() const = 0;
    virtual Msm8255RamPartition Partition(uint32_t index) const = 0;
};

}
