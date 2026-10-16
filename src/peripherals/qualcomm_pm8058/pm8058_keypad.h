#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <mutex>

class StateWriter;
class StateReader;

class Pm8058Keypad : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    /* Linux drivers/input/keyboard/pm8058-keypad.c: KP_DRV_MAX 18. */
    static constexpr uint32_t kMaxDrive = 18u;

    static constexpr uint16_t kRegCtrl    = 0x148u;
    static constexpr uint16_t kRegScan    = 0x149u;
    static constexpr uint16_t kRegNewData = 0x14Bu;
    static constexpr uint16_t kRegOldData = 0x14Cu;

    static bool Owns(uint16_t reg) {
        return reg == kRegCtrl || reg == kRegScan || reg == kRegNewData ||
               reg == kRegOldData;
    }

    uint8_t ReadReg(uint16_t reg);
    void    WriteReg(uint16_t reg, uint8_t value);

    void SetKeyPressed(uint32_t drive, uint32_t sense, bool pressed);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    uint32_t DriveLines() const;
    uint32_t SenseLines() const;

    uint8_t ReadDataPort(uint8_t (&set)[kMaxDrive], uint32_t& index);
    void    CaptureScan();
    void    PublishIrq();
    void    Reset();

    mutable std::mutex mtx_;

    uint8_t  ctrl_ = 0;
    uint8_t  scan_ = 0;

    uint8_t  state_[kMaxDrive]       = {};
    uint8_t  latched_new_[kMaxDrive] = {};
    uint8_t  latched_old_[kMaxDrive] = {};

    uint32_t events_    = 0;
    uint32_t new_index_ = 0;
    uint32_t old_index_ = 0;
};
