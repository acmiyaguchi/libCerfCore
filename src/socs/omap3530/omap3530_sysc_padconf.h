#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

class Omap3530SyscPadconf : public Peripheral {
public:
    using Peripheral::Peripheral;

    enum class PadPull { None, Down, Up };

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x48002000u; }
    uint32_t MmioSize() const override { return 0x00001000u; }

    uint16_t ReadHalf  (uint32_t addr) override;
    uint32_t ReadWord  (uint32_t addr) override;
    void     WriteHalf (uint32_t addr, uint16_t value) override;
    void     WriteWord (uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    bool     McBspClksFromPad(uint32_t port) const;
    bool     McBsp1ClkrFromClkx() const;
    bool     McBsp1FsrFromFsx() const;
    void     RegisterDevconfListener(std::function<void()> fn);
    uint32_t PadMuxMode(uint32_t pad_off) const;
    PadPull  PadPullOf(uint32_t pad_off) const;
    void     RegisterPadListener(uint32_t pad_off, std::function<void()> fn);

private:
    struct PadListener {
        uint32_t              pad_off;
        std::function<void()> fn;
    };

    uint32_t Word(uint32_t off) const;
    void     LoadPowerOnLocked();
    void     StoreWordLocked(uint32_t off, uint32_t value);
    void     AfterWrite(uint32_t off, uint32_t bytes);
    void     NotifyDevconf();

    mutable std::mutex mu_;
    std::array<uint16_t, 0x1000u / 2u> regs_{};
    std::vector<std::function<void()>> devconf_listeners_;
    std::vector<PadListener>           pad_listeners_;
    bool                               devconf_changed_ = false;
    bool                               pads_reset_      = false;
};
