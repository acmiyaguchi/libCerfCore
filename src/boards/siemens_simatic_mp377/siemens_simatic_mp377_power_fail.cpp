#include "siemens_simatic_mp377_power_fail.h"

#include "siemens_simatic_mp377_power_reset.h"

#include <cerfcore/boards/board_context.h>
#include "siemens_simatic_mp377_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/host_widget_registry.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../socs/irq_controller.h"

namespace cerfcore {

namespace siemens_simatic_mp377 {

bool SiemensSimaticMp377PowerFail::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && board->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMp377;
}

void SiemensSimaticMp377PowerFail::OnReady() {
    Reset();
    emu_.Get<HostWidgetRegistry>().Register(this);
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        if (!emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) Reset();
    });
}

void SiemensSimaticMp377PowerFail::SetAsserted(bool asserted) {
    asserted_.store(asserted, std::memory_order_release);
    RefreshIrq();
}

void SiemensSimaticMp377PowerFail::Reset() {
    asserted_.store(false, std::memory_order_release);
    RefreshIrq();
}

void SiemensSimaticMp377PowerFail::RefreshIrq() {
    auto& irq = emu_.Get<IrqController>();
    if (asserted_.load(std::memory_order_acquire))
        irq.AssertIrq(static_cast<int>(kMp377PowerFailIrqSource));
    else
        irq.DeAssertIrq(static_cast<int>(kMp377PowerFailIrqSource));
}

std::string SiemensSimaticMp377PowerFail::Tooltip() const {
    return asserted_.load(std::memory_order_acquire)
        ? "External power - failure asserted"
        : "External power - available";
}

WidgetIcon SiemensSimaticMp377PowerFail::Icon() const {
    return PowerInputIcon{asserted_.load(std::memory_order_acquire)};
}

std::vector<WidgetControl> SiemensSimaticMp377PowerFail::Controls() {
    ToggleControl input;
    input.id    = "power_fail";
    input.label = "Assert power-fail input";
    input.value = asserted_.load(std::memory_order_acquire);
    input.set   = [this](bool asserted) { SetAsserted(asserted); };
    return {WidgetControl{std::move(input)}};
}

bool SiemensSimaticMp377PowerFail::PollDirty() {
    const bool asserted = asserted_.load(std::memory_order_acquire);
    if (asserted == last_drawn_asserted_) return false;
    last_drawn_asserted_ = asserted;
    return true;
}

REGISTER_SERVICE(SiemensSimaticMp377PowerFail);

} // namespace siemens_mp377

}
