#include "folder_share_stage.h"

#include <cerfcore/peripherals/cerf_virt/cerf_virt_addr_map.h>
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../../execution/engine_bridge.h"

namespace cerfcore {

REGISTER_SERVICE(FolderShareStage);

bool FolderShareStage::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

uint32_t FolderShareStage::BasePa() const {
    return emu_.Get<BoardContext>().GuestAdditionsWindowBase() +
           CerfVirt::kFsStageOffset;
}

void FolderShareStage::OnReady() {
    EmulatedMemory& mem = emu_.Get<EmulatedMemory>();
    mem.AddRegion(BasePa(), CerfVirt::kFsStageSize, MemoryKind::kRam);
    base_ = mem.TryTranslate(BasePa());
    if (!base_) {
        LOG(Cerf, "[FolderShareStage] region at PA 0x%08X is not backed\n", BasePa());
        CerfFatalExit();
    }
    emu_.Get<EngineBridge>().Guest().SetDmaRegion(BasePa(), CerfVirt::kFsStageSize);
}

CerfVirt::ServerPB* FolderShareStage::Pb() {
    return reinterpret_cast<CerfVirt::ServerPB*>(base_ + CerfVirt::kFsStagePbOff);
}

uint8_t* FolderShareStage::IoBuf() {
    return base_ + CerfVirt::kFsStageIoOff;
}

}
