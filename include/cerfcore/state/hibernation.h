#pragma once

#include "../core/service.h"
#include "state_image_format.h"

#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

class StateWriter;
class StateReader;

/* Callers must run Save/Restore on a thread other than the JIT thread -
   JitRunner::Pause self-deadlocks if called from the JIT thread. */
class Hibernation : public Service {
public:
    using Service::Service;
    ~Hibernation() override;

    std::string DefaultStatePath() const;
    bool        DefaultStateExists() const;

    bool Save(const std::string& path);
    bool Restore(const std::string& path, bool ram_only = false);

    void SaveAsync(const std::string& path, std::function<void(bool)> on_done = {});
    void RestoreAsync(const std::string& path, bool ram_only = false);

    void RegisterPreSaveListener(std::function<void()> fn);

    void OnShutdown() override;

private:
    std::string      DeviceDirFile(const char* name) const;
    StateImageHeader LiveHeader() const;
    bool             WriteImage(const std::string& path, std::string& error);
    void             SaveSection(StateWriter& w, StateSection section);
    void             RestoreSection(StateReader& r, StateSection section);
    void             ReadHeader(StateReader& r);
    void             ApplyImage(StateReader& r, bool ram_only);
    bool             ApplyStateFile(StateReader& r, bool ram_only, std::string& reason);
    void             RollBack(const std::string& rollback_path);
    void             RestorePeripherals(StateReader& r);
    void             RestorePresentation(StateReader& r);
    uint32_t         PeripheralLayoutSig() const;
    void     Progress(const char* fmt, ...);
    void     JoinWorker();

    std::thread worker_;

    std::vector<std::function<void()>> pre_save_listeners_;
};
