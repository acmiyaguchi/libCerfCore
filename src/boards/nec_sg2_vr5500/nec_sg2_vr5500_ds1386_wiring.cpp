#include "../../peripherals/dallas_ds1386/ds1386_wiring.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/vrc5477/vrc5477_giu.h"
#include <cerfcore/boards/board_context.h>
#include "nec_sg2_vr5500_family_id.h"

namespace cerfcore {

namespace {

class NecSg2Vr5500Ds1386Wiring final : public Ds1386Wiring {
public:
    using Ds1386Wiring::Ds1386Wiring;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NecSg2Vr5500;
    }

    void OnReady() override { giu_ = &emu_.Get<Vrc5477Giu>(); }

    void SetIntA(bool active) override { giu_->DriveInterruptInput(active); }

    void SetIntB(bool) override {}

private:
    Vrc5477Giu* giu_ = nullptr;
};

REGISTER_SERVICE_AS(NecSg2Vr5500Ds1386Wiring, Ds1386Wiring);

}

}
