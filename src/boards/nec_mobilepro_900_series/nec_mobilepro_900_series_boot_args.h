#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

struct NecMobilepro900SeriesBootArgsLayout {
    uint32_t display_mode_pa;
    uint32_t core_mhz_pa;
    uint32_t reset_cause_pa;
    uint32_t boot_state_pa;
    uint32_t hard_boot_request_pa;
    uint32_t hard_boot_code_pa;
    uint32_t cleared_bytes;
    bool     has_low_battery_flag;
    uint32_t low_battery_pa;
    bool     hard_boot_restores_watchdog_cause;
    bool     has_main_status_words;
    uint32_t config_block_pointer_pa;
    uint32_t sleep_boot_key_flag_pa;
    bool     inits_ffuart;
    bool     sets_cpar;
};

class NecMobilepro900SeriesBootArgs : public Service {
public:
    using Service::Service;

    virtual const NecMobilepro900SeriesBootArgsLayout& Layout() const = 0;

protected:
    bool BoardMatchesKernelMajor(uint16_t major) const;
};

}
