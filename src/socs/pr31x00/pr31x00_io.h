#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

namespace cerfcore {

/* Philips PR31x00 I/O Module, TMPR3911/3912 ch.9. Registers $180-$198. */
class Pr31x00Io : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x10C00180u; }
    uint32_t MmioSize() const override { return 0x1Cu; }   /* $180-$19B */

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    /* A board part drives one multi-function I/O pin. Each edge latches into the
       interrupt module's MFIOPOSINT / MFIONEGINT status (§8.3.3, §8.3.4). */
    void DriveMfioInput(uint32_t pin, bool level);

    /* A board part drives one of the 7 general purpose I/O pins. A rising edge
       latches IOPOSINT[pin] and a falling edge IONEGINT[pin], both in Interrupt
       Status 5 (§8.3.5). */
    void DriveIoInput(uint32_t pin, bool level);

    /* Observe MFIODOUT ($184), for a peripheral wired to a multi-function output pin
       (a UART's DTR/RTS). `out_mask` is the pins the chip actually drives, so the
       observer must also fire on MFIODIREC and MFIOSEL: a direction change alone
       moves a line without touching MFIODOUT. Fired once at registration. */
    using MfioOutObserver = std::function<void(uint32_t mfio_dout, uint32_t out_mask)>;
    void RegisterMfioOutObserver(MfioOutObserver cb);

    /* Observe IODOUT (IO_CTL $180 <14:8>), same contract. IODOUT and IODIREC share
       IO_CTL, so one write carries both. */
    using IoOutObserver = std::function<void(uint32_t io_dout, uint32_t out_mask)>;
    void RegisterIoOutObserver(IoOutObserver cb);

    uint8_t  ReadByte(uint32_t addr) override { HaltUnsupportedAccess("PR31x00 IO ReadByte", addr, 0); }
    uint16_t ReadHalf(uint32_t addr) override { HaltUnsupportedAccess("PR31x00 IO ReadHalf", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("PR31x00 IO WriteByte", addr, v); }
    void WriteHalf(uint32_t addr, uint16_t v) override { HaltUnsupportedAccess("PR31x00 IO WriteHalf", addr, v); }

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    /* Reset values, §9.3.1-§9.3.7. IODIREC and MFIODIREC reset to 0 (all pins
       inputs); IODEBSEL, IODOUT and MFIODOUT reset undefined and are taken as 0. */
    uint32_t io_ctl_     = 0x00000000u;
    uint32_t mfio_dout_  = 0x00000000u;
    uint32_t mfio_direc_ = 0x00000000u;
    uint32_t mfio_sel_   = 0xF20F0FFFu;
    uint32_t io_pd_      = 0x0000007Fu;
    uint32_t mfio_pd_    = 0xFAF03FFCu;

    std::atomic<uint32_t> mfio_din_{0};
    std::atomic<uint32_t> io_din_{0};

    void NotifyMfioOut();
    void NotifyIoOut();

    std::vector<MfioOutObserver> mfio_out_observers_;
    std::vector<IoOutObserver>   io_out_observers_;
};

}
