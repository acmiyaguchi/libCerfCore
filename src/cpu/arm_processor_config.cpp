#include "arm_processor_config.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../core/cpu_config.h"

namespace cerfcore {

REGISTER_SERVICE(ArmProcessorConfig);

namespace {

constexpr std::pair<std::string_view, uint32_t> kPcStoreOffsets[] = {
    {"plus_8", 8u},
    {"plus_12", 12u},
};

constexpr std::pair<std::string_view, ArmSupersectionFormat> kSupersectionFormats[] = {
    {"unknown", ArmSupersectionFormat::kUnknown},
    {"none", ArmSupersectionFormat::kNone},
    {"pa32", ArmSupersectionFormat::kPa32},
    {"pa36", ArmSupersectionFormat::kPa36},
};

}

bool ArmProcessorConfig::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::kArm;
}

void ArmProcessorConfig::OnReady() {
    const auto& c = emu_.Get<CpuConfig>();

    midr_       = c.Number("midr");
    ctr_        = c.Number("ctr");
    clidr_      = c.Number("clidr");
    ccsidr_l1i_ = c.Number("ccsidr_l1i");
    ccsidr_l1d_ = c.Number("ccsidr_l1d");
    ccsidr_l2_  = c.Number("ccsidr_l2");

    pc_store_offset_           = c.Enum("pc_store_offset", kPcStoreOffsets);
    base_restored_abort_model_ = c.Boolean("base_restored_abort_model");
    supersection_format_       = c.Enum("supersection_format", kSupersectionFormats);

    thumb_                        = c.Boolean("thumb");
    thumb2_                       = c.Boolean("thumb2");
    dsp_                          = c.Boolean("dsp");
    load_store_double_            = c.Boolean("load_store_double");
    preload_                      = c.Boolean("preload");
    clz_                          = c.Boolean("clz");
    blx_reg_                      = c.Boolean("blx_reg");
    armv5_unconditional_space_    = c.Boolean("armv5_unconditional_space");
    load_to_pc_interworking_      = c.Boolean("load_to_pc_interworking");
    data_proc_to_pc_interworking_ = c.Boolean("data_proc_to_pc_interworking");
    accesses_spsr_in_system_mode_ = c.Boolean("accesses_spsr_in_system_mode");
    mls_                          = c.Boolean("mls");
    movw_movt_                    = c.Boolean("movw_movt");
    bit_field_                    = c.Boolean("bit_field");
    rev_                          = c.Boolean("rev");
    extend_rotate_                = c.Boolean("extend_rotate");
    ldrex_strex_                  = c.Boolean("ldrex_strex");
    ldrex_strex_v6k_              = c.Boolean("ldrex_strex_v6k");
    barrier_insn_                 = c.Boolean("barrier_insn");
    integer_divide_               = c.Boolean("integer_divide");

    cp15_v6_               = c.Boolean("cp15_v6");
    cp15_v7_               = c.Boolean("cp15_v7");
    security_extensions_   = c.Boolean("security_extensions");
    aux_control_register_  = c.Boolean("aux_control_register");
    l2_cache_aux_control_  = c.Boolean("l2_cache_aux_control");
    l1_system_array_debug_ = c.Boolean("l1_system_array_debug");

    vfp_   = c.Boolean("vfp");
    neon_  = c.Boolean("neon");
    fpsid_ = c.Number("fpsid");
    mvfr0_ = c.Number("mvfr0");
    mvfr1_ = c.Number("mvfr1");
}

/* ARM DDI 0406C.c B4.1.41 (p. B4-1555) CSSELR: Level bits[3:1], InD bit[0]. */
uint32_t ArmProcessorConfig::Ccsidr(uint32_t csselr) const {
    const uint32_t level = (csselr >> 1) & 0x7u;
    const uint32_t ind   =  csselr       & 0x1u;
    if (level == 0u) return ind ? ccsidr_l1i_ : ccsidr_l1d_;
    if (level == 1u) return ccsidr_l2_;
    return 0u;
}

}
