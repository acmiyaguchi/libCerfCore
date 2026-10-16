#pragma once

#include <cerfcore/core/service.h>
#include "../../host/battery_widget.h"
#include "../../execution/guest_cycle_clock.h"
#include "../../socs/rated_tick_count.h"
#include "../../socs/uart_line_device.h"

#include <cstdint>

class Sa11xxIntc;
class Sa11xxSp1Uart;

class HpJornada820Battery : public Service, public UartLineDevice {
public:
    explicit HpJornada820Battery(CerfEmulator& e) : Service(e) {}

    bool ShouldRegister() override;
    void OnReady() override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    void OnPacketEvent();
    void OnCpuRate();
    void ArmNextPacket();
    void SendPacket();

    BatteryWidget           battery_;
    GuestCycleClock*        clock_         = nullptr;
    GuestCycleClock::Event* packet_event_  = nullptr;
    Sa11xxIntc*             intc_          = nullptr;
    Sa11xxSp1Uart*          sp1_           = nullptr;
    RatedTickCount          packets_;
    uint64_t                next_packet_   = 0;
};
