#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <mutex>

namespace cerfcore {

namespace Log {

    #define LOG_CATEGORIES(X)                                                       \
        X(Cerf,        "CERF",        "generic emulator status (always on)")        \
        X(Caution,     "CAUTION",     "errors / warnings (always on)")              \
        X(Boot,        "BOOT",        "kernel boot / image loader")                 \
        X(Jit,         "JIT",         "ARM JIT runner")                             \
        X(Mem,         "MEM",         "emulated memory regions / telemetry")       \
        X(Net,         "NET",         "slirp backend / NDIS bridge")                \
        X(Periph,      "PERIPH",      "peripheral dispatcher")                      \
        X(Cp15,        "CP15",        "ARM cp15 system control coprocessor")        \
        X(Mmu,         "MMU",         "ARM MMU translation walker")                 \
        X(Tlb,         "TLB",         "ARM JIT page-table cache (GuestTlb)")        \
        X(Board,       "BOARD",       "board detection / BSP setup")                \
        X(Cfg,         "CFG",         "device config loader (always on)")           \
        X(SocIntc,     "SOC_INTC",    "any SoC interrupt controller")               \
        X(SocWdt,      "SOC_WDT",     "any SoC watchdog")                           \
        X(SocClkpwr,   "SOC_CLKPWR",  "any SoC clock / power block")                \
        X(SocIoport,   "SOC_IOPORT",  "any SoC GPIO / I-O port")                    \
        X(SocMemc,     "SOC_MEMC",    "any SoC memory controller")                  \
        X(SocUart,     "SOC_UART",    "any SoC UART / debug serial")                \
        X(Nkdbg,       "NKDBG",       "guest OS debug output: UART/serial TX + hooked OEM debug") \
        X(SocTimer,    "SOC_TIMER",   "any SoC PWM / system tick timer")            \
        X(SocDma,      "SOC_DMA",     "any SoC DMA controller")                     \
        X(SocRtc,      "SOC_RTC",     "any SoC real-time clock")                    \
        X(SocNand,     "SOC_NAND",    "any SoC NAND flash controller")              \
        X(SocIis,      "SOC_IIS",     "any SoC IIS / I2S audio interface")          \
        X(SocSpi,      "SOC_SPI",     "any SoC SPI interface")                      \
        X(SocGpu,      "SOC_GPU",     "any SoC GPU / graphics core")                \
        X(SocReset,    "SOC_RESET",   "any SoC reset controller")                   \
        X(UsbOtg,      "USB_OTG",     "USB OTG host/device controller")             \
        X(Lcd,         "LCD",         "host display window + per-SoC LCD ctrl")     \
        X(Pcmcia,      "PCMCIA",      "board-level PCMCIA bus controller (PD6710)") \
        X(Trace,       "TRACE",       "TraceManager + device-specific trace files") \
        X(Perf,        "PERF",        "RateProbe - per-second event counters")    \
        X(GuestDriver, "GUEST",       "CERF guest driver runtime output via cerf_debug_tx") \
        X(GuestAdditions, "GUEST_HOST", "CERF guest additions")                          \
        X(Discord,     "DISCORD",     "Discord Rich Presence")

    enum class Cat : uint8_t {
    #define X(name, slug, desc) name,
        LOG_CATEGORIES(X)
    #undef X
        COUNT
    };

    struct CatInfo {
        const char* slug;
        const char* desc;
    };

    inline constexpr CatInfo kCategories[] = {
    #define X(name, slug, desc) { slug, desc },
        LOG_CATEGORIES(X)
    #undef X
    };

    static_assert(sizeof(kCategories) / sizeof(CatInfo) == (size_t)Cat::COUNT,
                  "kCategories must match Cat::COUNT - X-macro out of sync");
    static_assert((int)Cat::COUNT <= 64,
                  "Log::Cat exceeds 64 - switch enabled_mask to std::bitset");

    inline constexpr uint64_t MASK_ALL =
        ((int)Cat::COUNT >= 64) ? ~0ULL : ((1ULL << (int)Cat::COUNT) - 1);
    inline constexpr uint64_t MASK_NONE = 0;

    inline constexpr uint64_t CatBit(Cat c) { return 1ULL << (int)c; }

    inline constexpr uint64_t MASK_PRODUCTION_DEFAULT =
          CatBit(Cat::Boot)
        | CatBit(Cat::Lcd)
        | CatBit(Cat::Mem)
        | CatBit(Cat::Net)
        | CatBit(Cat::GuestDriver)
        | CatBit(Cat::Mmu)
        | CatBit(Cat::Tlb)
        | CatBit(Cat::Jit)
        | CatBit(Cat::SocReset)
        | CatBit(Cat::Discord)
        | CatBit(Cat::GuestAdditions)
        | CatBit(Cat::Nkdbg);

    void InitDefaultLogFile();

    namespace detail {
        extern std::atomic<uint64_t> enabled_mask;
    }

    inline uint64_t GetEnabled() {
        return detail::enabled_mask.load(std::memory_order_relaxed);
    }
    inline constexpr bool IsAlwaysOn(Cat cat) {
        return cat == Cat::Cerf || cat == Cat::Caution || cat == Cat::Cfg;
    }
    inline bool IsEnabled(Cat cat) {
        if (IsAlwaysOn(cat)) return true;
        return (GetEnabled() & (1ULL << (int)cat)) != 0;
    }

    void SetEnabled(uint64_t mask);
    void SetFile(const char* path);
    const char* FilePath();
    void SetFlush(bool enabled);
    void SetAllowFlood(bool allow);
    void Close();

    void Print(Cat cat, const char* fmt, ...);

    void CopyRecentCautionsPostFreeze(char* out, size_t out_size);

    /* Comma-separated slugs, plus the special tokens "ALL" and "NONE".
       Unknown tokens warn on stderr and contribute nothing to the mask. */
    uint64_t ParseCategories(const char* str);

    /* Print every category and its description to stdout - used by --help. */
    void PrintCategoryList();

    bool SymbolizeAddress(const void* addr, char* out, size_t out_size);

    void EmergencyStart();
    bool IsEmergencyActive();
    void Emergency(const char* fmt, ...);
    void EmergencyWriteBytes(const char* buf, size_t len);
    void EmergencyPrintNativeStack(const char* tag);
    void EmergencyDumpAllThreadStacks();

    /* Installs a top-level SEH filter that runs the Emergency* crash dump for
       exceptions outside any CerfFatalExit / JIT __except - e.g. an access
       violation during service teardown, which otherwise terminates the
       process with no cerf.crash.log. Call once, early in main. */
    void InstallCrashHandler();

    using CrashPresenter = void (*)(const char* log_file);
    void SetCrashPresenter(CrashPresenter presenter);

}  // namespace Log

/* LOG(name, fmt, ...) - `name` is an unqualified Cat identifier (e.g. Boot,
   SocIntc). The macro qualifies it as Log::Cat::name; a typo is a compile
   error. The slug "[NAME]" is auto-prepended by Log::Print from the
   registry - never put it in the format string. */
#define LOG(name, ...) do {                                                         \
    if (::cerfcore::Log::IsEnabled(::cerfcore::Log::Cat::name))                     \
        ::cerfcore::Log::Print(::cerfcore::Log::Cat::name, __VA_ARGS__);            \
} while (0)

inline constexpr int CERF_FATAL_RUNTIME_ERROR = 1;
/* User/environment condition (unsupported board, no ROM installed), not a
   CERF bug: CerfFatalExit skips the crash-dump path for this code. */
inline constexpr int CERF_FATAL_USER_ERROR    = 2;
inline constexpr int CERF_FATAL_NORMAL_EXIT   = 0;
/* GNU coreutils timeout(1), EXIT STATUS: 124 if the command times out. */
inline constexpr int CERF_FATAL_TIMEOUT       = 124;

[[noreturn]] void CerfFatalExit(int code = CERF_FATAL_RUNTIME_ERROR);

}
