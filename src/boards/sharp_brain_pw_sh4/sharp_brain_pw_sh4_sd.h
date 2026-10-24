#pragma once

#include "../../peripherals/sd/sd_card_base.h"
#include "../../storage/disk_image.h"

#include <string>

namespace cerfcore {

/* sharp_brain_pw_sh4 sdhc.dll SHC_Init c0782c1c: SDHC2 (Index 2) drives SSP1. */
class SharpBrainPwSh4Sd : public SdCardBase {
public:
    using SdCardBase::SdCardBase;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t SlotIndex() const override { return 1u; }
    bool     IsCardPresent() const { return present_; }

protected:
    uint32_t SectorCount() const override { return sectors_; }
    void ReadBlock(uint32_t sector, uint8_t* out) override;
    void WriteBlock(uint32_t sector, const uint8_t* data) override;

private:
    std::string StorePath() const;
    void        OpenStore();

    DiskImage   store_;
    std::string path_;
    uint32_t    sectors_ = 0u;
    bool        present_ = false;
};

}
