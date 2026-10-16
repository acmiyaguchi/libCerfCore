#include <cerfcore/core/service.h>
#include <cerfcore/host/host_widget.h>
#include <cstdint>
#include <string>
#include <vector>
#include "ford_sync_gen2_temperature_section.h"
#include <cerfcore/boards/board_context.h>
#include "ford_sync_gen2_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/host_widget_registry.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

class FordSyncGen2CarWidget : public Service, public HostWidget {
public:
    explicit FordSyncGen2CarWidget(CerfEmulator& emu)
        : Service(emu), temperature_(emu), sections_{&temperature_} {}
    bool ShouldRegister() override;
    void OnReady() override;
    std::string WidgetName() const override { return "Car"; }
    WidgetGroup Group() const override { return WidgetGroup::Indicator; }
    std::string Tooltip() const override;
    std::vector<WidgetControl> Controls() override;
    WidgetIcon Icon() const override { return ResourceIcon{"ICON_CAR"}; }
    bool PollDirty() override;
    void SaveWidgetState(StateWriter& w) const override;
    void RestoreWidgetState(StateReader& r) override;
private:
    TemperatureSection temperature_;
    std::vector<WidgetSection*> sections_;
};

REGISTER_SERVICE(FordSyncGen2CarWidget);

bool FordSyncGen2CarWidget::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && board->GetBoardFamilyId() == BoardFamilyId::FordSyncGen2;
}
void FordSyncGen2CarWidget::OnReady() { emu_.Get<HostWidgetRegistry>().Register(this); }
std::string FordSyncGen2CarWidget::Tooltip() const {
    return "SYNC 2 car controls\n" + temperature_.Label();
}
std::vector<WidgetControl> FordSyncGen2CarWidget::Controls() {
    std::vector<WidgetControl> controls;
    for (auto* section : sections_)
        controls.push_back(WidgetControl{GroupControl{section->Label(), section->BuildControls()}});
    return controls;
}
bool FordSyncGen2CarWidget::PollDirty() {
    bool dirty = false;
    for (auto* section : sections_) dirty |= section->PollDirty();
    return dirty;
}
void FordSyncGen2CarWidget::SaveWidgetState(StateWriter& w) const { temperature_.SaveState(w); }
void FordSyncGen2CarWidget::RestoreWidgetState(StateReader& r) { temperature_.RestoreState(r); }
}

}
