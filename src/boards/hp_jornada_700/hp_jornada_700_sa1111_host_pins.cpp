#include "../../peripherals/intel_sa1111/sa1111_sbi.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../socs/sa11xx/sa11xx_test_unit.h"
#include <cerfcore/boards/board_context.h>
#include "hp_jornada_700_family_id.h"

namespace cerfcore {

namespace {

Sa1111Sbi::Mbgnt ToSbi(Sa11xxTestUnit::Mbgnt pin) {
    switch (pin) {
        case Sa11xxTestUnit::Mbgnt::Arbiter: return Sa1111Sbi::Mbgnt::Arbiter;
        case Sa11xxTestUnit::Mbgnt::Low:     return Sa1111Sbi::Mbgnt::Low;
        case Sa11xxTestUnit::Mbgnt::High:    return Sa1111Sbi::Mbgnt::High;
        case Sa11xxTestUnit::Mbgnt::Undetermined: break;
    }
    return Sa1111Sbi::Mbgnt::Undetermined;
}

/* SA-1111 Developer's Manual Table 1-2 (printed 1-4): MBGNT "Memory bus grant, from SA-1110
   processor." */
class HpJornada700Sa1111HostPins : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
    }

    void OnReady() override {
        auto& sbi = emu_.Get<Sa1111Sbi>();
        auto& tu  = emu_.Get<Sa11xxTestUnit>();
        sbi.SetHostPins([&tu] { return ToSbi(tu.MbgntPin()); });
        tu.RegisterChangeListener([&sbi] { sbi.OnHostPinsChange(); });
        emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([&sbi] { sbi.OnHostPinsChange(); });
    }
};

}

REGISTER_SERVICE(HpJornada700Sa1111HostPins);

}
