#pragma once

#include <cerfcore/core/service.h>
#include "../../boot/ce_imgfs_walker.h"

#include <cstdint>
#include <vector>

/* Parses the runtime IMGFS ("AppFS" ShadowROM) volume read back from the
   guest-provisioned Sync2 NAND. The volume is direct-addressed (non-FTL), so
   IMGFS logical byte L == index L into Volume() - overlay math relies on this. */
class Imx51NandImgfsView : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool     Located()        const { return located_; }
    uint64_t VolumeBasePage() const { return base_page_; }
    uint32_t BytesPerBlock()  const { return bpb_; }

    /* The reconstructed volume bytes: volume logical offset == index into this
       vector (imgfs_base 0). Empty until Located() && reconstructed. */
    const std::vector<uint8_t>& Volume() const { return vol_; }

    const std::vector<cerf::ce_imgfs_walker::ImgfsModule>& Modules() const {
        return modules_;
    }

private:
    bool LocateVolume();
    void ReconstructAndWalk();

    bool     located_   = false;
    uint64_t base_page_ = 0;
    uint32_t bpb_       = 0;
    std::vector<uint8_t> vol_;
    std::vector<cerf::ce_imgfs_walker::ImgfsModule> modules_;
};
