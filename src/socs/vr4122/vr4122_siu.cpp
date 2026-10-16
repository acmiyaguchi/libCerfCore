#include "../vr41xx/vr41xx_siu_reset_csel.h"

#include <cerfcore/boards/board_context.h>
#include "vr4122_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include "vr4122_dsiu.h"

#include <cstdint>

namespace {

/* NetBSD vripreg.h VR4122_SIU_ADDR 0x0F000800; DSIU follows at 0x0F000820. */
class Vr4122Siu : public Vr41xxSiuResetCsel {
public:
    using Vr41xxSiuResetCsel::Vr41xxSiuResetCsel;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Vr4122;
    }

    uint32_t MmioBase() const override { return 0x0F000800u; }

protected:
    /* SIURESET 0x0F000809: D1 DSIURESET "Resets DSIU", D0 SIURESET "Resets SIU", D7:2 RFU
       (VR4131 UM U15350EJ2V0UM 18.2.13 p357 == 20.2.14). */
    uint8_t SiuResetWritable() const override { return 0x03u; }
    void    ApplySiuReset(uint8_t sreset) override {
        if (sreset & 0x01u) Serial16550::Reset();
        if (sreset & 0x02u) emu_.Get<Vr4122Dsiu>().ResetCore();
    }
};

}  /* namespace */

REGISTER_SERVICE(Vr4122Siu);
