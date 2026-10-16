#include "arm_injection_band.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/cpu/emulated_memory.h>

REGISTER_SERVICE(ArmInjectionBand);

bool ArmInjectionBand::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::kArm;
}

void ArmInjectionBand::OnReady() {
    memory_ = &emu_.Get<EmulatedMemory>();
}

void ArmInjectionBand::Set(uint32_t va_base, uint32_t pa_base, uint32_t size) {
    va_base_ = va_base;
    pa_base_ = pa_base;
    size_    = size;
}

uint8_t* ArmInjectionBand::Serve(uint32_t va, cerfengine::ArmMmuAccess access,
                                 uint32_t& served_pa) {
    if (size_ == 0u) return nullptr;
    const uint32_t off = va - va_base_;
    if (off >= size_) return nullptr;
    const uint32_t pa = pa_base_ + off;
    const bool is_write = (access == cerfengine::ArmMmuAccess::kWrite ||
                           access == cerfengine::ArmMmuAccess::kReadWrite);
    uint8_t* host = is_write ? memory_->TryTranslateWrite(pa)
                             : memory_->TryTranslate(pa);
    if (!host) return nullptr;
    served_pa = pa;
    return host;
}
