#pragma once

#include <cerfcore/core/service.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

class S3C2410LevelSubSource {
public:
    virtual ~S3C2410LevelSubSource() = default;

    virtual bool SubSourceAsserted(int sub_source_bit) = 0;
};

class S3C2410SubSourceLevels : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    void Register(int main_source_bit, int sub_source_bit,
                  S3C2410LevelSubSource* source);

    void ReassertStillHeld(uint32_t cleared_bits);

private:
    struct Entry {
        int                    main_bit;
        int                    sub_bit;
        S3C2410LevelSubSource* source;
    };

    std::atomic<uint32_t> registered_mask_{0};
    std::mutex            mutex_;
    std::vector<Entry>    entries_;
};
