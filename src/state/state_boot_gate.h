#pragma once

#include <cerfcore/core/service.h>

/* Saved-state boot gate. Registers a JitRunner pre-start hook in OnReady;
   the hook runs on the main thread after Bootstrap, before the JIT spawns. */
class StateBootGate : public Service {
public:
    using Service::Service;
    void OnReady() override;

private:
    /* Pre-start hook (main thread): restore / warm-boot / cold-boot per
       DeviceConfig::boot_mode when a saved state image exists. */
    void Run();
};
