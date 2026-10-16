#include "cerf_virt_nic_stage.h"

#include <cerfcore/peripherals/cerf_virt/cerf_virt_addr_map.h>
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../../execution/engine_bridge.h"

REGISTER_SERVICE(CerfVirtNicStage);

bool CerfVirtNicStage::ShouldRegister() {
    const auto& cfg = emu_.Get<DeviceConfig>();
    return cfg.guest_additions && cfg.network_enabled;
}

uint32_t CerfVirtNicStage::BasePa() const {
    return emu_.Get<BoardContext>().GuestAdditionsWindowBase() +
           CerfVirt::kNicStageOffset;
}

void CerfVirtNicStage::OnReady() {
    EmulatedMemory& mem = emu_.Get<EmulatedMemory>();
    mem.AddRegion(BasePa(), CerfVirt::kNicStageSize, MemoryKind::kRam);
    base_ = mem.TryTranslate(BasePa());
    if (!base_) {
        emu_.Get<Fatal>().Die("[CerfVirtNicStage] region at PA 0x%08X is not backed",
                              BasePa());
    }
    emu_.Get<EngineBridge>().Guest().SetDmaRegion(BasePa(), CerfVirt::kNicStageSize);
}

uint8_t* CerfVirtNicStage::TxSlot(uint32_t index) {
    return base_ + CerfVirt::kNicTxBufOffset +
           (index % CerfVirt::kNicTxSlots) * CerfVirt::kNicSlotSize;
}

uint8_t* CerfVirtNicStage::RxSlot(uint32_t index) {
    return base_ + CerfVirt::kNicRxBufOffset +
           (index % CerfVirt::kNicRxSlots) * CerfVirt::kNicSlotSize;
}
