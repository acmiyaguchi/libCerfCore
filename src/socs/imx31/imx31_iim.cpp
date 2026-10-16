#include "../freescale_iim_impl.h"
#include "imx31_id.h"

namespace {

using cerf_freescale_iim_detail::FreescaleIimBase;

/* MCIMX31RM Table 13-2: SREV (0x24) = 0x14 = i.MX31 silicon rev 1.2 (M45G mask),
   the production silicon shipping when the Zune 30 launched (2006-09). */
constexpr uint32_t kOffSrev        = 0x24u;
constexpr uint32_t kSrevImx31Rev12 = 0x14u;

/* U-Boot mainline iim_regs: i.MX31 fuse bank 2 shadow at IIM +0x1000, 0x400
   bytes; the kernel reads 7 bytes within. */
constexpr uint32_t kBank2Start = 0x1000u;
constexpr uint32_t kBank2End   = 0x1400u;

class Imx31Iim : public FreescaleIimBase<0x5001C000u, SocId::Imx31> {
public:
    using FreescaleIimBase::FreescaleIimBase;

protected:
    std::optional<uint32_t> ReadRegister(uint32_t off) const override {
        if (off == kOffSrev) return kSrevImx31Rev12;
        return std::nullopt;
    }
    bool IsFuseShadow(uint32_t off) const override {
        return off >= kBank2Start && off < kBank2End;
    }
};

}  /* namespace */

REGISTER_SERVICE(Imx31Iim);
