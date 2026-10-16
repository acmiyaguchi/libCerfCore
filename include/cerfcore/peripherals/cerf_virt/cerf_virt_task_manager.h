#pragma once

#include "../peripheral_base.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace cerfcore {

class CerfVirtTaskManager : public Peripheral {
public:
    using Peripheral::Peripheral;

    struct ProcEntry {
        uint32_t     pid;
        uint32_t     parent_pid;
        uint32_t     thread_count;
        int32_t      base_priority;
        uint32_t       mem_base;
        std::u16string name;
    };
    struct WinEntry {
        uint32_t       hwnd;
        uint32_t       pid;
        uint32_t       thread_id;
        bool           visible;
        std::u16string title;
    };
    struct Snapshot {
        uint64_t gen = 0;
        uint32_t guest_total = 0;
        std::vector<ProcEntry> procs;
        std::vector<WinEntry>  wins;
    };
    struct ActionResult {
        uint32_t ticket;
        uint32_t code;
        bool     ok;
        uint32_t guest_err;
    };

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override;
    uint32_t MmioSize() const override;
    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void     RequestProcessList();
    void     RequestWindowList();
    uint32_t RequestKill(uint32_t pid);
    uint32_t RequestSwitchTo(uint32_t pid);
    uint32_t RequestSwitchToWindow(uint32_t hwnd);
    uint32_t RequestRun(const std::u16string& cmdline);
    Snapshot GetSnapshot();
    std::optional<ActionResult> TakeActionResult();

private:
    struct PendingCmd {
        uint32_t     ticket;
        uint32_t     code;
        uint32_t       pid;
        std::u16string run_text;
    };

    uint32_t EnqueueLocked(uint32_t code, uint32_t pid, std::u16string run_text);
    void     PublishNextLocked();
    void     ConsumeResponseLocked();

    std::mutex             mtx_;
    std::deque<PendingCmd> queue_;
    bool                   in_flight_      = false;
    uint32_t               in_flight_code_ = 0;
    bool                   list_live_      = false;
    uint32_t               next_ticket_    = 0;

    uint32_t     cmd_gen_  = 0;
    uint32_t     cmd_code_ = 0;
    uint32_t       cmd_pid_  = 0;
    std::u16string cmd_run_text_;

    uint32_t resp_cmd_gen_ = 0;
    uint32_t resp_status_  = 0;
    uint32_t resp_err_     = 0;
    uint32_t resp_count_   = 0;
    uint32_t resp_total_   = 0;

    uint32_t               rec_words_[37] = {};
    uint32_t               rec_index_     = 0;
    std::vector<ProcEntry> pending_rows_;
    std::vector<WinEntry>  pending_wins_;

    void ConsumeRecordLocked();

    Snapshot                    snap_;
    std::optional<ActionResult> last_action_;
};

}
