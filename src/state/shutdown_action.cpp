#include <cerfcore/state/shutdown_action.h>

#include <cerfcore/boot/guest_cold_boot.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/shutdown_host.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/hibernation.h>
#include <cerfcore/state/shutdown_choice.h>

REGISTER_SERVICE(ShutdownAction);

void ShutdownAction::Perform(ShutdownChoice c) {
    auto& host = emu_.Get<ShutdownHost>();
    switch (c) {
        case ShutdownChoice::Cancel:
            return;
        case ShutdownChoice::SoftReset:
            emu_.Get<GuestCpuReset>().WarmReset();
            return;
        case ShutdownChoice::HardReset:
            emu_.Get<GuestColdBoot>().RequestHardReset();
            return;
        case ShutdownChoice::ExitSave:
            emu_.Get<Hibernation>().SaveAsync("", [this](bool saved) {
                emu_.Get<ShutdownHost>().RunPromptJob([this, saved] {
                    auto& h = emu_.Get<ShutdownHost>();
                    if (saved) h.BeginTeardown();
                    else       h.CancelTeardown();
                });
            });
            return;
        case ShutdownChoice::Exit:
            host.BeginTeardown();
            return;
    }
}
