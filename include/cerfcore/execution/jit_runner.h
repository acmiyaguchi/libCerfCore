#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "../core/service.h"

class JitRunner : public Service {
public:
    using Service::Service;
    ~JitRunner() override;

    void OnShutdown() override;

    void Start();

    void RegisterPreStartHook(std::function<void()> fn);

    void Join();

    void RequestStop();

    bool Stopped() const { return stopped_.load(std::memory_order_acquire); }

    bool Started() const { return started_.load(std::memory_order_acquire); }

    void Pause();
    void Resume();

private:
    void PublishHostChainExit(const std::unique_lock<std::mutex>&);

    void RunLoop();

    std::thread             thread_;
    std::atomic<bool>       stop_requested_{false};
    std::atomic<bool>       stopped_{false};
    std::atomic<bool>       started_{false};

    std::atomic<bool>       pause_requested_{false};
    bool                    paused_ = false;
    std::mutex              pause_mutex_;
    std::condition_variable pause_cv_;

    std::vector<std::function<void()>> pre_start_hooks_;
};
