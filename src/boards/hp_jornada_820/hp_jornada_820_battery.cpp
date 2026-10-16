#include "hp_jornada_820_battery.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/host_widget_registry.h>
#include "../../socs/sa11xx/sa11xx_intc.h"
#include "../../socs/sa11xx/sa11xx_sp1_uart.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/boards/board_context.h>
#include "hp_jornada_820_family_id.h"

#include <array>
#include <cstdint>

namespace {

constexpr uint8_t kFrameStart = 0xE0u;
constexpr uint8_t kFrameEnd   = 0xE1u;
constexpr GuestCycleClock::Rate kPacketRate{1u, 6u};

/* jornada820 nk.exe sub_8005A858 case 0x22 (SYSINTR 34, hplib sub_11A25D8): ICMR |= 0x8000. */
constexpr uint32_t kSp1IntcMask = 1u << 15;

/* jornada820 gwes sub_8FB14: [2] BatteryLifePercent; [3,4] capacity LE, minutes =
   capacity - 5 for BatteryLifeTime; [12] bit 2 = charging. */
std::array<uint8_t, 14> BuildPacket(int percent, bool on_battery) {
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    const uint16_t cap = static_cast<uint16_t>(percent + 5);
    std::array<uint8_t, 14> p{};
    p[0]  = kFrameStart;
    p[2]  = static_cast<uint8_t>(percent);
    cerf::le::Put16(p.data() + 3, cap);
    p[12] = on_battery ? 0x00u : 0x04u;
    p[13] = kFrameEnd;
    return p;
}

}

bool HpJornada820Battery::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada820;
}

void HpJornada820Battery::OnReady() {
    clock_         = &emu_.Get<GuestCycleClock>();
    packet_event_  = clock_->Add([this] { OnPacketEvent(); });
    clock_->RegisterRateListener([this] { OnCpuRate(); });
    intc_          = &emu_.Get<Sa11xxIntc>();
    sp1_           = &emu_.Get<Sa11xxSp1Uart>();
    sp1_->AttachDevice(this);
    emu_.Get<HostWidgetRegistry>().Register(&battery_);
    packets_.SetRate(clock_->ClockRate(), kPacketRate);
    packets_.Start(clock_->Cycles());
    ArmNextPacket();
}

void HpJornada820Battery::SendPacket() {
    if ((intc_->GetIcmr() & kSp1IntcMask) == 0u) return;
    const auto pkt = BuildPacket(battery_.FillPercent(), battery_.IsOnBattery());
    sp1_->PushRxBurstNow(pkt.data(), pkt.size());
}

void HpJornada820Battery::ArmNextPacket() {
    clock_->Arm(packet_event_, packets_.CycleOfTick(next_packet_));
}

void HpJornada820Battery::OnPacketEvent() {
    next_packet_ = packets_.TicksAt(clock_->Cycles()) + 1u;
    SendPacket();
    ArmNextPacket();
}

void HpJornada820Battery::OnCpuRate() {
    packets_.Rescale(clock_->Cycles(), clock_->ClockRate(), kPacketRate);
    ArmNextPacket();
}

void HpJornada820Battery::SaveState(StateWriter& w) {
    const TickPosition pos = packets_.PositionAt(clock_->Cycles());
    w.Write<uint64_t>("batt_packet_ticks", pos.ticks);
    w.Write<uint64_t>("batt_packet_phase", pos.phase);
    w.Write<uint64_t>("batt_packet_phase_den", pos.phase_den);
}

void HpJornada820Battery::RestoreState(StateReader& r) {
    TickPosition pos;
    r.Read("batt_packet_ticks", pos.ticks);
    r.Read("batt_packet_phase", pos.phase);
    r.Read("batt_packet_phase_den", pos.phase_den);
    packets_.SetRate(clock_->ClockRate(), kPacketRate);
    packets_.PlaceAt(clock_->Cycles(), pos);
    next_packet_ = packets_.TicksAt(clock_->Cycles()) + 1u;
    ArmNextPacket();
}

REGISTER_SERVICE(HpJornada820Battery);
