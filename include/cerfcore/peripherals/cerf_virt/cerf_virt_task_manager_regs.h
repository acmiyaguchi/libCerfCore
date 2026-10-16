#pragma once

#include <cstdint>

namespace CerfVirt {

constexpr uint32_t kTmCmdGen     = 0x00;
constexpr uint32_t kTmCmdCode    = 0x04;
constexpr uint32_t kTmCmdPid     = 0x08;

constexpr uint32_t kTmCmdRunLen  = 0x0C;
constexpr uint32_t kTmCmdRunText = 0x100;

constexpr uint32_t kTmRespCmdGen  = 0x40;
constexpr uint32_t kTmRespStatus  = 0x44;
constexpr uint32_t kTmRespErr     = 0x48;
constexpr uint32_t kTmRespCount   = 0x50;
constexpr uint32_t kTmRespTotal   = 0x54;
constexpr uint32_t kTmRecIndex    = 0x60;
constexpr uint32_t kTmRecKick     = 0x64;
constexpr uint32_t kTmRespKick    = 0x80;

constexpr uint32_t kTmRecData     = 0x200;

constexpr uint32_t kTmCmdNone         = 0;
constexpr uint32_t kTmCmdList         = 1;
constexpr uint32_t kTmCmdKill         = 2;
constexpr uint32_t kTmCmdSwitchTo     = 3;
constexpr uint32_t kTmCmdRun          = 4;
constexpr uint32_t kTmCmdListWindows  = 5;
constexpr uint32_t kTmCmdSwitchToWin  = 6;

constexpr uint32_t kTmRunMaxWchars     = 256;
constexpr uint32_t kTmMaxProcRecords   = 256;
constexpr uint32_t kTmProcNameWchars   = 64;
constexpr uint32_t kTmWindowTitleWchars = 64;

struct TaskManagerProcRecord {
    uint32_t pid;
    uint32_t parent_pid;
    uint32_t thread_count;
    int32_t  base_priority;
    uint32_t mem_base;
    uint16_t name[kTmProcNameWchars];
};

static_assert(sizeof(TaskManagerProcRecord) == 148,
              "record layout must stay byte-identical to the guest pump's");

struct TaskManagerWindowRecord {
    uint32_t hwnd;
    uint32_t pid;
    uint32_t thread_id;
    uint32_t flags;
    uint16_t title[kTmWindowTitleWchars];
};

constexpr uint32_t kTmWinFlagVisible = 0x1;

static_assert(sizeof(TaskManagerWindowRecord) == 144,
              "record layout must stay byte-identical to the guest pump's");

}
