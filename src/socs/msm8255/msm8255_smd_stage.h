#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

class Msm8255SmdStage : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t BasePa() const { return base_pa_; }
    uint32_t WriteBasePa() const { return base_pa_ + kStageBytes; }
    static constexpr uint32_t ReadCapacity() { return kStageBytes; }
    static constexpr uint32_t WriteCapacity() { return kWriteStageBytes; }

    uint32_t Linearize(uint32_t fifo_pa, uint32_t half, uint32_t tail,
                       uint32_t avail);
    void     Scatter(uint32_t ring_pa, uint32_t half, uint32_t head,
                     uint32_t bytes);

private:
    static constexpr uint32_t kStageBytes      = 0x1000u;
    static constexpr uint32_t kWriteStageBytes = 0x10000u;

    uint32_t base_pa_                      = 0u;
    uint8_t  buf_[kStageBytes]             = {};
    uint8_t  write_buf_[kWriteStageBytes]  = {};
};
