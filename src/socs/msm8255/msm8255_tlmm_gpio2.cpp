#include "msm8255_gpio_window_base.h"

#include <cstdint>

namespace {

/* Linux arch/arm/mach-msm msm_iomap-7x30.h: MSM7X30_GPIO2_PHYS 0xAC101000,
   MSM7X30_GPIO2_SIZE SZ_4K. */
constexpr uint32_t kGpio2Base = 0xAC101000u;
constexpr uint32_t kGpio2Size = 0x00001000u;

/* Linux arch/arm/mach-msm gpio_hw.h CONFIG_ARCH_MSM7X30: bank 1's OUT, OE,
   INT_EDGE, INT_POS, INT_EN, INT_CLEAR and INT_STATUS are MSM_GPIO2_REG(0x00,
   0x08, 0x50, 0x58, 0x60, 0x68, 0x70), +0x400. */
constexpr Msm8255GpioBank kBanks[] = {
    {0x400u, 0x408u, 0x450u, 0x458u, 0x460u, 0x468u, 0x470u, 16u, 43u},
};

constexpr uint32_t kBankCount = sizeof(kBanks) / sizeof(kBanks[0]);

constexpr uint32_t kMuxSelectNumber = 0x410u;
constexpr uint32_t kMuxConfigNumber = 0x414u;

/* Linux arch/arm/mach-msm irqs-7x30.h INT_GPIO_GROUP2 (32 + 19). */
constexpr int kGroupVicLine = 51;

class Msm8255TlmmGpio2
    : public cerf_msm8255_gpio_detail::Msm8255GpioWindowBase<
          kGpio2Base, kGpio2Size, kBankCount, kBanks, kMuxSelectNumber,
          kMuxConfigNumber, kGroupVicLine> {
public:
    using Msm8255GpioWindowBase::Msm8255GpioWindowBase;
};

}

REGISTER_SERVICE(Msm8255TlmmGpio2);
