#pragma once

#include "../pcmcia/pcmcia_card.h"
#include "../serial/serial_16550.h"
#include "../serial/serial_endpoint_kind.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace cerfcore {

class SerialEndpoint;

class SerialPcCard : public PcmciaCard {
public:
    static constexpr const char* kDisplayName        = serial_endpoint_kind::kModemName;
    static constexpr const char* kForwardDisplayName = serial_endpoint_kind::kForwardName;

    explicit SerialPcCard(CerfEmulator& emu);
    SerialPcCard(CerfEmulator& emu, std::string host_com_port);
    ~SerialPcCard() override;

    std::string DisplayName() const override {
        return mode_ == Mode::HostForward ? kForwardDisplayName : kDisplayName;
    }
    std::string TooltipDetail() const override;
    const char* IconResource() const override {
        return mode_ == Mode::HostForward ? "ICON_PCMCIA_SERIAL_COM"
                                          : "ICON_PCMCIA_SERIAL_MODEM";
    }

    void OnInserted() override;

    const char* SaveId() const override {
        return mode_ == Mode::HostForward ? "serial_fwd" : "serial";
    }
    std::string SaveBinding() const override {
        return mode_ == Mode::HostForward ? host_port_ : std::string();
    }
    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;
    void OnShutdown() override;

    void PowerOn () override;
    void PowerOff() override;

    uint8_t ReadAttribute8 (uint32_t offset)                override;
    void    WriteAttribute8(uint32_t offset, uint8_t value) override;

    uint8_t  ReadCommon8  (uint32_t offset)                 override;
    uint16_t ReadCommon16 (uint32_t offset)                 override;
    void     WriteCommon8 (uint32_t offset, uint8_t  value) override;
    void     WriteCommon16(uint32_t offset, uint16_t value) override;

    uint8_t  ReadIo8  (uint32_t offset)                     override;
    uint16_t ReadIo16 (uint32_t offset)                     override;
    void     WriteIo8 (uint32_t offset, uint8_t  value)     override;
    void     WriteIo16(uint32_t offset, uint16_t value)     override;

private:
    void OnUartIrq(bool asserted);
    void DriveIrqLineLow();
    void BuildCis();   /* mode-dependent CIS (FUNCE present only for the modem) */

    static constexpr uint32_t kConfigBase = 0x200u;
    static constexpr uint32_t kCorOffset  = kConfigBase + 0u;   /* FCR_OFFSET_COR  */
    static constexpr uint32_t kFcsrOffset = kConfigBase + 2u;   /* FCR_OFFSET_FCSR */
    /* I/O window: the 8 16550 registers at the CISTPL_CFTABLE_ENTRY base. */
    static constexpr uint32_t kIoBase   = 0x3F8u;
    static constexpr uint32_t kRegCount = 8u;

    uint8_t cor_      = 0;
    uint8_t fcsr_     = 0;
    bool    uart_irq_ = false;
    bool    powered_  = false;   /* no Vcc -> the card drives no pin, IRQ included */

    enum class Mode { Modem, HostForward };
    Mode         mode_ = Mode::Modem;
    std::string  host_port_;
    std::vector<uint8_t> cis_;   /* attribute-memory CIS, built per mode */

    /* The endpoint detaches through a raw SerialLine* at the UART in its dtor, so the
       UART must outlive it. */
    std::unique_ptr<Serial16550>    uart_;
    std::unique_ptr<SerialEndpoint> endpoint_;
};

}
