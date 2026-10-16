#include "../vr41xx/vr41xx_bcu_impl.h"

#include "vr4111_bus_error.h"

#include <cstdint>
#include "vr4111_id.h"

namespace {

using cerf_vr41xx_reg_window_detail::OtherReset;
using cerf_vr41xx_reg_window_detail::ReadKind;
using cerf_vr41xx_reg_window_detail::Vr41xxBcuBase;
using cerf_vr41xx_reg_window_detail::Vr41xxRegWindowModel;
using cerf_vr41xx_reg_window_detail::WriteKind;

/* BCU 0x0B000000-0x0B00001F, DMAA follows at 0x0B000020 (VR4111 UM Table 6-10 p170);
   register offsets from UM Table 11-1 p263. */
constexpr Vr41xxRegWindowModel kModel = {
    /*base=*/0x0B000000u,
    /*size=*/0x20u,
    /*num_regs=*/12u,
    /*word_pairs=*/false,
    {
        /* 0x00 BCUCNTREG1 (UM 11.2.1 p264): R/W D15/14/13/12/10/8/6/4/1/0,
           RFU-read-0 D11/9/7/5/3/2, RTCRST and Other-resets rows both 0. */
        { ReadKind::kStored, WriteKind::kStored, 0xF553u, 0x0000u, 0x0AACu },
        /* 0x02 BCUCNTREG2 (UM 11.2.2 p266): R/W D0 GMODE, D15:1 RFU-read-0,
           both reset rows 0. */
        { ReadKind::kStored, WriteKind::kStored, 0x0001u, 0x0000u, 0xFFFEu },
        {},
        {},
        {},
        /* 0x0A BCUSPEEDREG (UM 11.2.3 p267): R/W D13:12 WPROM, D10:8 WLCD/M,
           D6:4 WISAA, D2:0 WROMA, RFU-read-0 D15/14/11/7/3, both rows 0. */
        { ReadKind::kStored, WriteKind::kStored, 0x3777u, 0x0000u, 0xC888u },
        {},
        /* 0x0E BCURFCNTREG (UM 11.2.5 p270): R/W D13:0 BRF, RFU-read-0 D15:14;
           RTCRST 0x0200; Other resets 0 on D15:14, Undefined on D13:0. */
        { ReadKind::kStored, WriteKind::kStored, 0x3FFFu, 0x0200u, 0xC000u,
          OtherReset::kReset, 0x3FFFu },
    },
};

using cerf_vr41xx_reg_window_detail::WritableAndWrite0Partition;

static_assert(WritableAndWrite0Partition(kModel, 0x00u) &&
                  WritableAndWrite0Partition(kModel, 0x02u) &&
                  WritableAndWrite0Partition(kModel, 0x0Au) &&
                  WritableAndWrite0Partition(kModel, 0x0Eu),
              "VR4111 BCU writable and write-0 RFU bits must partition all 16");

constexpr uint32_t kOffSpeed  = 0x0Au;
constexpr uint16_t kWlcdmHigh = 0x0400u;

const char* SpeedEncodingRejected(uint16_t speed, uint16_t cnt1) {
    if (((speed >> 12) & 0x3u) == 0x3u) return "BCUSPEEDREG write with the RFU WPROM 11";
    if (((speed >> 5) & 0x3u) == 0x3u) return "BCUSPEEDREG write with an RFU WISAA 11x";
    if ((speed & kWlcdmHigh) != 0u && (cnt1 & vr41xx_bcu::kCnt1IsamLcd) == 0u) {
        return "BCUSPEEDREG WLCD/M 1xx is RFU while BCUCNTREG1 ISAM/LCD = 0";
    }
    return nullptr;
}

/* UM 11.2.4 p269 BCUERRSTREG (0x0B00 000C): D0 BERRST R/W1C, D[15..1] Reserved R
   "Write 0 to these bits.  0 is returned after a read." */
constexpr uint32_t kErrStAddr     = 0x0B00000Cu;
constexpr uint16_t kErrStReserved = 0xFFFEu;

class Vr4111Bcu : public Vr41xxBcuBase<SocId::Vr4111, kModel> {
public:
    using Vr41xxBcuBase::Vr41xxBcuBase;

    uint16_t ReadHalf(uint32_t addr) override {
        if (addr == kErrStAddr) return emu_.Get<Vr4111BusError>().ReadStatus();
        return Vr41xxBcuBase::ReadHalf(addr);
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        if (addr != kErrStAddr) {
            Vr41xxBcuBase::WriteHalf(addr, value);
            return;
        }
        if (value & kErrStReserved) {
            HaltUnsupportedAccess("WriteHalf sets a Reserved bit", addr, value);
        }
        emu_.Get<Vr4111BusError>().WriteStatus(value);
    }

    void SaveState(StateWriter& w) override {
        Vr41xxBcuBase::SaveState(w);
        emu_.Get<Vr4111BusError>().SaveState(w);
    }

    void RestoreState(StateReader& r) override {
        Vr41xxBcuBase::RestoreState(r);
        emu_.Get<Vr4111BusError>().RestoreState(r);
    }

    void PostRestore() override { emu_.Get<Vr4111BusError>().PostRestore(); }

protected:
    const char* EncodingRejected(uint32_t off, uint16_t value) const override {
        if (off == kOffSpeed) {
            return SpeedEncodingRejected(value, StoredReg(vr41xx_bcu::kOffCnt1 / 2u));
        }
        if (off == vr41xx_bcu::kOffCnt1) {
            return SpeedEncodingRejected(StoredReg(kOffSpeed / 2u), value);
        }
        return nullptr;
    }
};

}

REGISTER_SERVICE(Vr4111Bcu);
