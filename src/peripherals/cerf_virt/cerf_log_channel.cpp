#include "cerf_log_channel.h"

#include <cerfcore/peripherals/cerf_virt/cerf_virt_addr_map.h>
#include "../peripheral_dispatcher.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/log.h>
#include <cerfcore/host/hw_screen.h>

REGISTER_SERVICE(CerfLogChannelPeripheral);

namespace {

const char* ChannelTag(uint32_t id) {
    switch (id) {
        case CerfVirt::kLogChannelIdStub:          return "[guest-stub] ";
        case CerfVirt::kLogChannelIdDisplay:       return "[guest] ";
        case CerfVirt::kLogChannelIdSharedFolders: return "[guest-fs] ";
        default:                                   return "[guest-?] ";
    }
}

}

bool CerfLogChannelPeripheral::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

void CerfLogChannelPeripheral::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint32_t CerfLogChannelPeripheral::MmioBase() const {
    return emu_.Get<BoardContext>().GuestAdditionsWindowBase() + CerfVirt::kLogChannelOffset;
}
uint32_t CerfLogChannelPeripheral::MmioSize() const { return CerfVirt::kLogChannelSize; }

uint8_t  CerfLogChannelPeripheral::ReadByte (uint32_t ) { return 0u; }
uint16_t CerfLogChannelPeripheral::ReadHalf (uint32_t ) { return 0u; }
uint32_t CerfLogChannelPeripheral::ReadWord (uint32_t ) { return 0u; }

void CerfLogChannelPeripheral::WriteByte(uint32_t addr, uint8_t value) {
    Dispatch(addr, static_cast<char>(value));
}
void CerfLogChannelPeripheral::WriteHalf(uint32_t addr, uint16_t value) {
    Dispatch(addr, static_cast<char>(value & 0xFFu));
}
void CerfLogChannelPeripheral::WriteWord(uint32_t addr, uint32_t value) {
    Dispatch(addr, static_cast<char>(value & 0xFFu));
}

void CerfLogChannelPeripheral::Dispatch(uint32_t addr, char c) {
    const uint32_t off  = addr - MmioBase();
    const uint32_t id   = off / CerfVirt::kLogChannelStride;
    const uint32_t slot = off % CerfVirt::kLogChannelStride;
    if (slot == CerfVirt::kLogChannelFatalSlot) Fatal(id);
    else                                        AppendChar(id, c);
}

void CerfLogChannelPeripheral::Fatal(uint32_t id) {
    AppendChar(id, '\n');
    LOG(GuestDriver, "%sFATAL raised by guest driver - halting\n", ChannelTag(id));
    emu_.Get<HwScreen>().AddLine(std::string(ChannelTag(id)) +
                                 "FATAL raised by guest driver - halting");
    CerfFatalExit();
}

void CerfLogChannelPeripheral::AppendChar(uint32_t id, char c) {
    if (id >= CerfVirt::kLogChannelCount) return;
    std::string flush;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string& buf = line_[id];
        if (c == '\n' || c == '\r') {
            if (buf.empty()) return;
            flush.swap(buf);
        } else {
            buf.push_back(c);
            if (buf.size() < 4096u) return;
            flush.swap(buf);
        }
    }
    LOG(GuestDriver, "%s%s\n", ChannelTag(id), flush.c_str());
    emu_.Get<HwScreen>().AddLine(ChannelTag(id) + flush);
}
