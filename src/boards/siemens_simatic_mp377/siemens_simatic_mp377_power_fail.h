#pragma once

#include <cerfcore/core/service.h>
#include <cerfcore/host/host_widget.h>

#include <atomic>
#include <string>
#include <vector>

namespace cerfcore {

namespace siemens_simatic_mp377 {

/* siemens_mp377_v1040, PowerFail.dll: initialization 0x02951988-0x02951A24;
   IstPowerFail 0x02951670-0x02951900. */
class SiemensSimaticMp377PowerFail final : public Service, public HostWidget {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;
    void SetAsserted(bool asserted);

    std::string WidgetName() const override { return "External power"; }
    WidgetGroup Group() const override { return WidgetGroup::Power; }
    std::string Tooltip() const override;
    WidgetIcon Icon() const override;
    std::vector<WidgetControl> Controls() override;
    bool PollDirty() override;

private:
    void Reset();
    void RefreshIrq();

    std::atomic<bool> asserted_{false};
    bool last_drawn_asserted_ = false;
};

} // namespace siemens_mp377

}
