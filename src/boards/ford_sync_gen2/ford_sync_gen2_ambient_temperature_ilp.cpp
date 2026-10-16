#include "ford_sync_gen2_ambient_temperature.h"
#include "ford_sync_gen2_ilp_channel.h"
#include "ford_sync_gen2_ilp_signals.h"
#include <cerfcore/boards/board_context.h>
#include "ford_sync_gen2_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace {
class FordSyncGen2AmbientTemperatureIlp : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && board->GetBoardFamilyId() == BoardFamilyId::FordSyncGen2;
    }
    void OnReady() override {
        emu_.Get<FordSyncGen2IlpChannel>().RegisterDevice({
            "ambient-temperature",
            [this](bool force) { Refresh(force); },
            {},
            {},
            {},
            [this](StateWriter& w) { emu_.Get<FordSyncGen2AmbientTemperature>().SaveState(w); },
            [this](StateReader& r) { emu_.Get<FordSyncGen2AmbientTemperature>().RestoreState(r); },
            [this] { emu_.Get<FordSyncGen2AmbientTemperature>().Clear(); }
        });
        Refresh(true);
    }
private:
    uint64_t revision_ = UINT64_MAX;
    void Refresh(bool force) {
        auto& signals = emu_.Get<FordSyncGen2IlpSignals>();
        const auto ambient = emu_.Get<FordSyncGen2AmbientTemperature>().Read();
        if (force || ambient.revision != revision_) {
            /* EA5T-14D544-BA.sec, VNIGeneralSvc.dll sub_C1595D40. */
            if (ambient.available)
                signals.SetReportedValue(0x02000205, ambient.half_celsius ? *ambient.half_celsius + 80 : 0xFE);
            else signals.ClearReportedValue(0x02000205);
            revision_ = ambient.revision;
        }
    }
};
REGISTER_SERVICE(FordSyncGen2AmbientTemperatureIlp);
}
