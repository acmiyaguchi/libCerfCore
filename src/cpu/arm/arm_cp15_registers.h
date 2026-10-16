#pragma once

#include <cstdint>

#include <cerfcore/core/force_inline.h>
#include <cerfcore/core/service.h>
#include <cerfengine/arm/arm_mmu_state.h>
#include <cerfengine/arm/arm_system_host.h>

namespace cerfcore {

class ArmProcessorConfig;

union ArmDfsr {
    uint32_t word;
    struct {
        uint32_t status       : 4;
        uint32_t domain       : 4;
        uint32_t reserved_8_9 : 2;
        uint32_t fs4          : 1;
        uint32_t wnr          : 1;
        uint32_t ext          : 1;
        uint32_t cm           : 1;
        uint32_t reserved     : 18;
    } bits;
};
static_assert(sizeof(ArmDfsr) == 4);

/* ARM DDI 0406C.c Table B3-23, Short-descriptor FS encodings. */
namespace ArmFaultStatus {
    constexpr uint32_t kExternalAbort             = 0b01000;
    constexpr uint32_t kAlignment                 = 0b00001;
    constexpr uint32_t kTranslationSection        = 0b00101;
    constexpr uint32_t kTranslationPage           = 0b00111;
    constexpr uint32_t kDomainSection             = 0b01001;
    constexpr uint32_t kDomainPage                = 0b01011;
    constexpr uint32_t kPermissionSection         = 0b01101;
    constexpr uint32_t kPermissionPage            = 0b01111;
    constexpr uint32_t kExternalAbortTranslation1 = 0b01100;
    constexpr uint32_t kExternalAbortTranslation2 = 0b01110;
}

struct ArmCp15File {
    uint32_t aux_control    = 0;
    uint32_t ttbr0          = 0;
    uint32_t dacr           = 0;
    uint32_t dfsr           = 0;
    uint32_t dfar           = 0;
    uint32_t ifsr           = 0;
    uint32_t ifar           = 0;
    uint32_t csselr         = 0;
    uint32_t ttbr1          = 0;
    uint32_t ttbcr          = 0;
    uint32_t prrr           = 0;
    uint32_t nmrr           = 0;
    uint32_t tpidrurw       = 0;
    uint32_t tpidruro       = 0;
    uint32_t tpidrprw       = 0;
    uint32_t l2_aux_control = 0;
};

class ArmCp15Registers : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    ArmCp15File& File() { return file_; }

    void RaiseAbort(uint32_t va, uint32_t fault_status, uint32_t domain, cerfengine::ArmMmuAccess access);

    cerfengine::ArmResetResult Reset();
    void SetPendingResumeMmu(uint32_t control, uint32_t ttbr0, uint32_t dacr);
    void SetPendingResumeAuxControl(uint32_t aux);

    static uint32_t CERFENGINE_FASTCALL ReadCcsidr(void* self);

    template <typename F>
    static constexpr void VisitFaultRegisters(ArmCp15File& s, F& field) {
        field("aux_control_register", s.aux_control);
        field("translation_table_base", s.ttbr0);
        field("domain_access_control", s.dacr);
        field("fault_status", s.dfsr);
        field("fault_address", s.dfar);
        field("ifsr", s.ifsr);
        field("ifar", s.ifar);
    }

    template <typename F>
    static constexpr void VisitTableRegisters(ArmCp15File& s, F& field) {
        field("cssel_register", s.csselr);
        field("ttbr1", s.ttbr1);
        field("ttbcr", s.ttbcr);
        field("prrr", s.prrr);
        field("nmrr", s.nmrr);
    }

    template <typename F>
    static constexpr void VisitThreadRegisters(ArmCp15File& s, F& field) {
        field("tpidrurw", s.tpidrurw);
        field("tpidruro", s.tpidruro);
        field("tpidrprw", s.tpidrprw);
        field("l2_aux_control", s.l2_aux_control);
    }

private:
    friend void CERFENGINE_FASTCALL cerfengine::host::ArmAlignmentFault(void* context,
                                                                        uint32_t va,
                                                                        bool is_write);

    CERF_FORCEINLINE void RecordAlignmentFault(uint32_t va, bool is_write);

    ArmCp15File         file_{};
    ArmProcessorConfig* config_ = nullptr;

    bool     pending_mmu_     = false;
    uint32_t pending_control_ = 0;
    uint32_t pending_ttbr0_   = 0;
    uint32_t pending_dacr_    = 0;
    bool     pending_aux_     = false;
    uint32_t pending_aux_value_ = 0;
};

}
