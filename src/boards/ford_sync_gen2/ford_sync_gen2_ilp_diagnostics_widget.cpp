#include "ford_sync_gen2_ilp_channel.h"
#include "ford_sync_gen2_ilp_signals.h"
#include <cerfcore/boards/board_context.h>
#include "ford_sync_gen2_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/host_widget.h>
#include <cerfcore/host/host_widget_registry.h>
#include <cstdio>

namespace cerfcore {

namespace {
class FordSyncGen2IlpDiagnosticsWidget : public Service, public HostWidget {
public:
    using Service::Service;
    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && board->GetBoardFamilyId() == BoardFamilyId::FordSyncGen2;
    }
    void OnReady() override { emu_.Get<HostWidgetRegistry>().Register(this); }
    std::string WidgetName() const override { return "ILP diagnostics"; }
    WidgetGroup Group() const override { return WidgetGroup::Debug; }
    WidgetIcon Icon() const override {
        const bool rejected = Failures(emu_.Get<FordSyncGen2IlpChannel>().ReadCounters()) > dismissed_failures_;
        return ResourceIcon{rejected ? "ICON_ILP_DIAGNOSTICS_REJECTED" : "ICON_ILP_DIAGNOSTICS"};
    }
    std::string Tooltip() const override {
        const auto c = emu_.Get<FordSyncGen2IlpChannel>().ReadCounters();
        return "ILP messages: received " + std::to_string(c.received) + ", sent " + std::to_string(c.sent) +
            "; Outcomes: accepted " + std::to_string(c.accepted) + ", unsupported " +
            std::to_string(c.unsupported) + ", invalid " + std::to_string(c.invalid) +
            ", unavailable " + std::to_string(c.unavailable) + ", malformed " + std::to_string(c.malformed);
    }
    bool PollDirty() override {
        const auto counters = emu_.Get<FordSyncGen2IlpChannel>().ReadCounters();
        if (counters.received != last_received_) MarkRx();
        if (counters.sent != last_sent_) MarkTx();
        last_received_ = counters.received;
        last_sent_ = counters.sent;
        auto current = Tooltip();
        if (current == last_tooltip_) return false;
        last_tooltip_ = std::move(current);
        return true;
    }
    std::vector<WidgetControl> Controls() override {
        std::vector<WidgetControl> controls{WidgetControl{InfoControl{Tooltip()}}};
        const auto failures = Failures(emu_.Get<FordSyncGen2IlpChannel>().ReadCounters());
        ActionControl dismiss;
        dismiss.id      = "dismiss_rejection";
        dismiss.label   = "Dismiss rejection indicator";
        dismiss.enabled = failures > dismissed_failures_;
        dismiss.press   = [this, failures] {
            dismissed_failures_ = failures;
            last_tooltip_.clear();
        };
        controls.push_back(WidgetControl{std::move(dismiss)});
        auto& signals = emu_.Get<FordSyncGen2IlpSignals>();
        for (std::size_t start = 0; start < signals.GroundedSignalCount(); start += 32) {
            GroupControl group;
            group.label = "Status signals " + std::to_string(start + 1);
            for (std::size_t i = start; i < start + 32 && i < signals.GroundedSignalCount(); ++i) {
                const auto id = signals.GroundedSignalIdAt(i);
                char text[96];
                if (signals.IsReported(id)) std::snprintf(text, std::size(text), "0x%08X = %llu", id,
                    static_cast<unsigned long long>(signals.ReportedValue(id)));
                else std::snprintf(text, std::size(text), "0x%08X: Not reported", id);
                group.children.push_back(WidgetControl{InfoControl{text}});
            }
            controls.push_back(WidgetControl{std::move(group)});
        }
        return controls;
    }
private:
    static uint64_t Failures(const FordSyncGen2IlpChannel::Counters& counters) {
        return counters.unsupported + counters.invalid + counters.unavailable + counters.malformed;
    }
    std::string last_tooltip_;
    uint64_t last_received_ = 0, last_sent_ = 0, dismissed_failures_ = 0;
};
REGISTER_SERVICE(FordSyncGen2IlpDiagnosticsWidget);
}

}
