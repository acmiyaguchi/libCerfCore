#include "mips_processor_config.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/config_json.h>
#include "../core/cpu_config.h"

#include <string>

namespace cerfcore {

REGISTER_SERVICE(MipsProcessorConfig);

namespace {

constexpr std::pair<std::string_view, cerfengine::MipsIsaLevel> kIsaLevels[] = {
    {"mips1", cerfengine::MipsIsaLevel::kMips1},
    {"mips3", cerfengine::MipsIsaLevel::kMips3},
    {"mips4", cerfengine::MipsIsaLevel::kMips4},
};

constexpr std::pair<std::string_view, uint32_t> kMinPageShifts[] = {
    {"1kb", 10u},
    {"4kb", 12u},
};

}

bool MipsProcessorConfig::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::kMips;
}

void MipsProcessorConfig::OnReady() {
    const auto& c = emu_.Get<CpuConfig>();

    prid_           = c.Number("prid");
    isa_level_      = c.Enum("isa_level", kIsaLevels);
    tlb_size_       = c.Number("tlb_size");
    min_page_shift_ = c.Enum("min_page_size", kMinPageShifts);
    phys_addr_mask_ = c.Number("phys_addr_mask");
    device_ip_mask_ = c.Number("device_ip_mask");

    if (tlb_size_ == 0u || tlb_size_ > cerfengine::kMipsTlbMax)
        CfgFatal(c.Origin("tlb_size"), "cpu_config.tlb_size must be between 1 and " +
                                           std::to_string(cerfengine::kMipsTlbMax));

    fpu_                = c.Boolean("fpu");
    llsc_               = c.Boolean("llsc");
    counter_            = c.Boolean("counter");
    watch_              = c.Boolean("watch");
    vr41xx_power_modes_ = c.Boolean("vr41xx_power_modes");
    mips16_             = c.Boolean("mips16");
    tx39_multiply_add_  = c.Boolean("tx39_multiply_add");
    cop1_cop2_unusable_ = c.Boolean("cop1_cop2_unusable");
}

}
