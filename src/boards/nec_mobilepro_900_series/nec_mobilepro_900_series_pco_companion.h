#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../peripherals/serial/serial_endpoint.h"
#include "../../socs/pxa2xx/pxa2xx_gpio.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>

namespace cerfengine { class HostRequestChannel; }

namespace cerfcore {

class NecMobilepro900SeriesLed;
class NecMobilepro900SeriesPicLoader;

class NecMobilepro900SeriesPcoCompanion : public Service, public SerialEndpoint {
public:
    using KeyMatrix = std::array<uint8_t, 13>;

    /* nec_mobilepro_900_ce4_2 keybddr.dll sub_1BD3530: a set bit reads as key up. */
    static constexpr KeyMatrix kIdleMatrix = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
                                              0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};

    using Service::Service;
    bool ShouldRegister() override;
    void OnReady() override;

    void OnGuestTx(const uint8_t* data, size_t n) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

    void SetMainBatteryRaw(uint16_t raw) { main_battery_raw_.store(raw, std::memory_order_release); }
    uint16_t MainBatteryRaw() const { return main_battery_raw_.load(std::memory_order_acquire); }

    void    QueueKeyState(const KeyMatrix& matrix);
    uint8_t KeyMatrixByte(size_t i);

    void AttachTouch(std::function<void()> on_report_rate, std::function<void()> on_restore);
    GuestCycleClock::Rate ReportRate() const;
    void SendTouch(uint16_t x, uint16_t y);
    void SendPenUp();

private:
    struct KeyStep {
        KeyMatrix matrix;
        uint64_t  arrival;
        bool      key_down;
    };

    void OnResetPin(const Pxa2xxGpio::OutputPins& pins, bool resync);
    void ResetPic();
    bool PicRunning() const;
    void OnBtuartTx(uint8_t b);
    void OnCommand();
    void PushByte(uint8_t b);
    void SendReply(uint8_t id, const uint8_t* data, size_t n);

    void     OnHostRequest();
    void     SettleKeys(uint64_t now);
    void     Present(const KeyStep& step, uint64_t at);
    uint64_t HoldEndFrom(uint64_t start) const;
    void     ArmKeyDown();
    void     ResetKeys();

    std::mutex            report_mtx_;
    std::atomic<uint16_t> main_battery_raw_{0};

    uint8_t                    cmd_[3]           = {};
    uint8_t                    cmd_len_          = 0;
    uint8_t                    data_left_        = 0;
    uint8_t                    report_period_ms_ = 0;
    bool                       pen_down_         = false;
    bool                       touch_enabled_    = true;
    bool                       key_notify_       = true;
    bool                       pic_held_         = false;
    std::array<uint8_t, 512>   stored_data_{};

    NecMobilepro900SeriesLed*       led_    = nullptr;
    NecMobilepro900SeriesPicLoader* loader_ = nullptr;

    std::function<void()> on_report_rate_;
    std::function<void()> on_touch_restore_;

    std::mutex              key_mtx_;
    std::deque<KeyMatrix>   host_keys_;
    std::deque<KeyStep>     key_steps_;
    KeyMatrix               cur_matrix_    = kIdleMatrix;
    KeyMatrix               last_queued_   = kIdleMatrix;
    uint64_t                cur_at_        = 0;
    bool                    key_presented_ = false;
    GuestCycleClock*        clock_         = nullptr;
    GuestCycleClock::Event* key_event_     = nullptr;
    cerfengine::HostRequestChannel*     host_requests_ = nullptr;
};

}
