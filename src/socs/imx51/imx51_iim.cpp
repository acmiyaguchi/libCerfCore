#include "../freescale_iim_impl.h"
#include "imx51_id.h"

namespace {

using cerf_freescale_iim_detail::FreescaleIimBase;

/* i.MX51 IIM at PA 0x83F9_8000 (MCIMX51RM Table 2-1). The four fuse banks are
   shadow-mapped at IIM offsets 0x800/0xC00/0x1000/0x1400, 0x80 rows each
   (MCIMX51RM Ch 6 Fuse Map). The CE kernel reads bank-0 rows 0x820-0x83C. */
constexpr uint32_t kFuseBankStride = 0x400u;
constexpr uint32_t kFuseBank0      = 0x800u;
constexpr uint32_t kFuseBankRows   = 0x80u;   /* 32 rows x 4 bytes per bank */
constexpr uint32_t kFuseBankCount  = 4u;

class Imx51Iim : public FreescaleIimBase<0x83F98000u, SocId::Imx51> {
public:
    using FreescaleIimBase::FreescaleIimBase;

protected:
    std::optional<uint32_t> ReadRegister(uint32_t /*off*/) const override {
        return std::nullopt;  /* no ID/control register read by the guest yet */
    }
    bool IsFuseShadow(uint32_t off) const override {
        for (uint32_t bank = 0; bank < kFuseBankCount; ++bank) {
            const uint32_t start = kFuseBank0 + bank * kFuseBankStride;
            if (off >= start && off < start + kFuseBankRows) return true;
        }
        return false;
    }
};

}  /* namespace */

REGISTER_SERVICE(Imx51Iim);
