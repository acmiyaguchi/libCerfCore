#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include "../execution/guest_cycle_clock.h"
#include "../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include "../tracing/kernel_debug_sink.h"
#include "freescale_module_clocks.h"
#include "freescale_sdma_bus.h"
#include "freescale_uart_autobaud.h"
#include "freescale_uart_clocks.h"
#include "freescale_uart_escape.h"
#include "freescale_uart_receiver.h"
#include "freescale_uart_regs.h"
#include "freescale_uart_sdma.h"
#include "freescale_uart_transmitter.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include "imx51/imx51_id.h"
#include "uart_endpoint.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace cerf_freescale_uart_detail {

template <uint32_t kBase, int kUartNum, const std::string_view& kSoc>
class FreescaleUartBase : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == kSoc;
    }

    void OnReady() override {
        clock_         = &emu_.Get<GuestCycleClock>();
        module_clocks_ = &emu_.Get<FreescaleModuleClocks>();
        timer_clocks_  = &emu_.Get<FreescaleTimerClocks>();
        uart_clocks_   = &emu_.Get<FreescaleUartClocks>();
        clock_->RegisterIdleListener([this] { OnLowPower(); });
        clock_->RegisterIdleExitListener([this] { OnLowPower(); });
        module_clocks_->RegisterGateListener([this] { ApplyBaud(); });
        uart_clocks_->RegisterRateListener([this] { ApplyBaud(); });
        clock_->RegisterRateListener([this] { escape_.Rescale(); });
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { HardReset(); });
        emu_.Get<PeripheralDispatcher>().Register(this);
        escape_.Attach();
        HardReset();
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t a) override {
        const uint32_t off = a - kBase;
        return static_cast<uint8_t>(Reg(off & ~3u) >> ((off & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t a) override {
        const uint32_t off = a - kBase;
        return static_cast<uint16_t>(Reg(off & ~3u) >> ((off & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t a) override { return Reg(a - kBase); }

    void WriteByte(uint32_t a, uint8_t v) override { Wr(a - kBase, v, (a & 3u) * 8u, 0xFFu); }
    void WriteHalf(uint32_t a, uint16_t v) override { Wr(a - kBase, v, (a & 2u) * 8u, 0xFFFFu); }
    void WriteWord(uint32_t a, uint32_t v) override { Wr(a - kBase, v, 0u, 0xFFFFFFFFu); }

    void SaveState(StateWriter& w) override {
        w.WriteBytes("ctrl", ctrl_, sizeof(ctrl_));
        w.Write("brm_ubir", active_ubir_);
        w.Write("brm_ubmr", active_ubmr_);
        w.Write<uint8_t>("ubir_written", ubir_written_ ? 1u : 0u);
        w.Write("line_tick", line_tick_);
        w.Write<uint8_t>("lp_mode", static_cast<uint8_t>(lp_mode_));
        w.Write<uint8_t>("lp_module_on", module_on_ ? 1u : 0u);
        w.Write<uint8_t>("lp_held", held_ ? 1u : 0u);
        w.Write<uint8_t>("brm_running", brm_running_ ? 1u : 0u);
        tx_.Save(w);
        rx_.Save(w);
        autobaud_.Save(w);
        escape_.Save(w);
        sdma_.Save(w);
        if (endpoint_) endpoint_->SaveState(w);
    }

    void RestoreState(StateReader& r) override {
        uint8_t written = 0, mode = 0, module_on = 0, held = 0, brm_running = 0;
        r.ReadBytes("ctrl", ctrl_, sizeof(ctrl_));
        r.Read("brm_ubir", active_ubir_);
        r.Read("brm_ubmr", active_ubmr_);
        r.Read("ubir_written", written);
        r.Read("line_tick", line_tick_);
        r.Read("lp_mode", mode);
        r.Read("lp_module_on", module_on);
        r.Read("lp_held", held);
        r.Read("brm_running", brm_running);
        ubir_written_ = written != 0u;
        lp_mode_      = static_cast<FreescaleLowPowerMode>(mode);
        module_on_    = module_on != 0u;
        held_         = held != 0u;
        brm_running_  = brm_running != 0u;
        tx_.Restore(r);
        rx_.Restore(r);
        autobaud_.Restore(r);
        escape_.Restore(r);
        sdma_.Restore(r);
        if (endpoint_) endpoint_->RestoreState(r);
    }

    void PostRestore() override {
        line_ = !Level();
        Settle();
    }

    void AttachEndpoint(UartEndpoint* ep) { endpoint_ = ep; }

    void BindSdma(FreescaleSdmaChannelHost* host, uint32_t tx_event, uint32_t rx_event) {
        sdma_.Bind(host, tx_event, rx_event, [this] { Sync(); });
    }

    void InjectRx(const uint8_t* data, size_t n) {
        rx_.Inject(in_sync_ ? reply_tick_ : line_tick_, data, n);
        Sync();
    }

protected:
    virtual void DriveIrq(bool) {}

private:
    static_assert(kUartNum == 1 || kUartNum == 2 || kUartNum == 3 || kUartNum == 5);
    static constexpr uint32_t kCtrlCount = cerf_freescale_uart::kCtrlCount;

    /* MCIMX51RM section 59.4.2.1 (page 59-34): peripheral_clock is the TxFIFO write and RxFIFO read
       clock; module_clock "is for all the state machines, writing RxFIFO, reading TxFIFO". */
    static constexpr FreescaleModule kModule =
        kUartNum == 1 ? FreescaleModule::kUart1 : kUartNum == 2 ? FreescaleModule::kUart2
        : kUartNum == 3 ? FreescaleModule::kUart3 : FreescaleModule::kUart5;
    static constexpr FreescaleModule kIpgModule =
        kUartNum == 1 ? FreescaleModule::kUart1Ipg : kUartNum == 2 ? FreescaleModule::kUart2Ipg
        : kUartNum == 3 ? FreescaleModule::kUart3Ipg : FreescaleModule::kUart5Ipg;

    /* ONEMS (0xB0) is 24-bit only on i.MX51 (MCIMX51RM §59.3.1); on i.MX31 every
       UART register including ONEMS is 16 LSB (MCIMX31RM §31.3.2). */
    static constexpr uint32_t kOnemsMask =
        (kSoc == SocId::Imx51) ? 0xFFFFFFu : 0xFFFFu;

    uint32_t& C(uint32_t off) { return ctrl_[(off - cerf_freescale_uart::kCtrlLo) / 4u]; }
    uint32_t  C(uint32_t off) const { return ctrl_[(off - cerf_freescale_uart::kCtrlLo) / 4u]; }

    uint32_t Txtl() const { return cerf_freescale_uart::TxtlOf(C(cerf_freescale_uart::kOffUfcr)); }
    uint32_t Rxtl() const { return cerf_freescale_uart::RxtlOf(C(cerf_freescale_uart::kOffUfcr)); }

    void HardReset() {
        for (uint32_t i = 0; i < kCtrlCount; ++i) ctrl_[i] = cerf_freescale_uart::kCtrlReset[i];
        C(cerf_freescale_uart::kOffUcr2) &= ~cerf_freescale_uart::kUcr2Srst;
        active_ubir_ = active_ubmr_ = 0u;
        ubir_written_ = false;
        line_tick_    = 0u;
        lp_mode_      = FreescaleLowPowerMode::kRun;
        tx_.Reset();
        rx_.Reset(true);
        autobaud_.Reset();
        escape_.Reset();
        sdma_.Reset();
        line_ = true;
        ApplyBaud();
    }

    /* MCIMX51RM Table 59-9 SRST: "Reset the transmit and receive state machines, all FIFOs and
       register USR1, USR2, UBIR, UBMR, UBRC, URXD, UTXD and UTS[6-3]". */
    void SoftReset() {
        using namespace cerf_freescale_uart;
        tx_.Reset();
        rx_.Reset(false);
        autobaud_.Reset();
        escape_.Reset();
        C(kOffUbir) = C(kOffUbmr) = 0u;
        active_ubir_ = active_ubmr_ = 0u;
        ubir_written_ = false;
        ApplyBaud();
    }

    /* MCIMX51RM section 59.4.8.2 (page 59-59): "The internal baud rate clocks of the transmitter and
       receiver are gated off if the stop_req signal to UART is asserted". */
    bool ModuleOn() const {
        return module_clocks_->Running(kModule) && lp_mode_ != FreescaleLowPowerMode::kStop &&
               lp_mode_ != FreescaleLowPowerMode::kStateRetention;
    }

    /* MCIMX51RM Table 59-29 (page 59-59): in the Doze State with the DOZE bit set the UART serial and
       IrDA interface is OFF while the UART clock stays ON. */
    bool Held() const {
        return lp_mode_ == FreescaleLowPowerMode::kDoze &&
               (C(cerf_freescale_uart::kOffUcr1) & cerf_freescale_uart::kUcr1Doze) != 0u;
    }

    void OnLowPower() {
        const FreescaleLowPowerMode mode =
            clock_->InIdle() ? timer_clocks_->WfiMode() : FreescaleLowPowerMode::kRun;
        if (mode == lp_mode_) return;
        lp_mode_ = mode;
        if (ModuleOn() != module_on_ || Held() != held_) ApplyBaud();
    }

    /* MCIMX51RM section 59.4.6 (page 59-52): "The UBIR register must be written before writing to the
       UBMR register. If only one register is written to by the software, the BRM continues to use
       the previous values." */
    void ApplyBrm() {
        using namespace cerf_freescale_uart;
        const GuestCycleClock::Rate module = uart_clocks_->ModuleClockRate();
        module_on_ = ModuleOn();
        const bool run = module_on_ && module.num != 0u;
        escape_.SetReference(module, RefDivider(C(kOffUfcr)), run);
        if (run == brm_running_) return;
        brm_running_ = run;
        LOG(SocUart, "UART%d: module clock %llu/%llu Hz %s, UFCR %04X UBIR %04X UBMR %04X\n", kUartNum,
            static_cast<unsigned long long>(module.num), static_cast<unsigned long long>(module.den),
            run ? "running" : "stopped", C(kOffUfcr), active_ubir_, active_ubmr_);
    }

    void ApplyBaud() {
        ApplyBrm();
        Configure();
    }

    void Configure() {
        using namespace cerf_freescale_uart;
        const uint32_t ucr1 = C(kOffUcr1), ucr2 = C(kOffUcr2), uts = C(kOffUts);
        held_ = Held();
        const FreescaleUartRxConfig rx = RxConfigOf(ctrl_, held_);
        tx_.Configure(line_tick_, (ucr1 & kUcr1Uarten) != 0u && (ucr2 & kUcr2Txen) != 0u, held_,
                      (ucr1 & kUcr1Sndbrk) != 0u, (uts & kUtsFrcperr) != 0u, rx.frame, events_);
        rx_.Configure(line_tick_, rx);
        autobaud_.Configure((ucr1 & kUcr1Adbr) != 0u, (C(kOffUcr3) & kUcr3Adnimp) != 0u);
        Handle();
        Sync();
    }

    void Handle() {
        using Kind = FreescaleUartTxEvent::Kind;
        const bool loops = cerf_freescale_uart::TxLoopsToRx(ctrl_);
        for (size_t i = 0; i < events_.size(); ++i) {
            const FreescaleUartTxEvent e = events_[i];
            switch (e.kind) {
                case Kind::kStarted:
                    if (loops) rx_.LoopChar(e.tick, e.byte, e.parity_error, rx_events_);
                    break;
                case Kind::kFinished:
                    reply_tick_ = e.tick;
                    if (!cerf_freescale_uart::IrMode(ctrl_)) EmitTx(e.byte);
                    break;
                case Kind::kBreak:
                    if (loops) rx_.LoopBreak(e.tick, rx_events_);
                    break;
                case Kind::kBreakEnd:
                    if (loops) rx_.LoopBreakEnd(e.tick);
                    break;
            }
        }
        events_.clear();
        HandleRx();
    }

    void HandleRx() {
        using namespace cerf_freescale_uart;
        if (rx_events_.empty()) return;
        if (autobaud_.Handle(rx_events_, active_ubir_, active_ubmr_)) AutobaudDone();
        escape_.Handle(rx_events_, (C(kOffUcr2) & kUcr2Escen) != 0u, C(kOffUesc), EscapeRefClocks());
        rx_events_.clear();
    }

    /* MCIMX51RM section 59.4.5.6 (page 59-48): at the end of the measurement "UBIR = 0x000F" and
       "UBMR = UBRC - 1". */
    void AutobaudDone() {
        using namespace cerf_freescale_uart;
        C(kOffUbir)   = 0x000Fu;
        C(kOffUbmr)   = static_cast<uint16_t>(autobaud_.Ubrc() - 1u);
        active_ubir_  = C(kOffUbir);
        active_ubmr_  = C(kOffUbmr);
        ubir_written_ = false;
        brm_dirty_    = true;
    }

    /* MCIMX51RM Table 59-28 (page 59-51): "(UTIM_Value + 1) x 0.002 = Time_Interval"; ONEMS "must
       contain the value of the UART internal frequency divided by 1000". */
    uint64_t EscapeRefClocks() const {
        using namespace cerf_freescale_uart;
        return (uint64_t{C(kOffUtim)} + 1u) * 2u * C(kOffOnems);
    }

    FreescaleUartDmaEnables Enables() const {
        using namespace cerf_freescale_uart;
        FreescaleUartDmaEnables en;
        en.tx    = (C(kOffUcr1) & kUcr1Txdmaen) != 0u;
        en.rx    = (C(kOffUcr1) & kUcr1Rxdmaen) != 0u;
        en.aging = (C(kOffUcr1) & kUcr1Atdmaen) != 0u;
        en.idle  = (C(kOffUcr4) & kUcr4Iddmaen) != 0u;
        en.txtl  = Txtl();
        en.rxtl  = Rxtl();
        return en;
    }

    uint64_t NextTick() const {
        const uint64_t tx = tx_.NextTick(), rx = rx_.NextTick();
        return tx < rx ? tx : rx;
    }

    uint64_t RunnableTick() const {
        return brm_running_ ? NextTick() : FreescaleUartReceiver::kNever;
    }

    void Sync() {
        if (in_sync_) return;
        in_sync_ = true;
        for (;;) {
            const uint64_t next = RunnableTick();
            if (next != FreescaleUartReceiver::kNever) {
                line_tick_ = next;
                tx_.Advance(next, events_);
                Handle();
                rx_.Advance(next, rx_events_);
                HandleRx();
            }
            if (brm_dirty_) {
                brm_dirty_ = false;
                ApplyBrm();
            }
            sdma_.Service(line_tick_, Enables());
            if (next == FreescaleUartReceiver::kNever && RunnableTick() == FreescaleUartReceiver::kNever)
                break;
        }
        in_sync_ = false;
        Settle();
    }

    void Settle() {
        if (!brm_running_ || held_) rx_.EdgeWithoutClock();
        const bool level = Level();
        if (level != line_) {
            line_ = level;
            DriveIrq(level);
        }
    }

    uint32_t Usr1() const {
        using namespace cerf_freescale_uart;
        return rx_.Usr1() | escape_.Usr1() | kUsr1Rtss | (tx_.Level() <= Txtl() ? kUsr1Trdy : 0u);
    }

    /* MCIMX51RM Table 59-14 RIIN: "In DCE mode this bit is always zero"; Table 59-12 DCEDTE: "1 DTE
       mode selected". */
    uint32_t Usr2() const {
        using namespace cerf_freescale_uart;
        const uint32_t riin = (C(kOffUfcr) & kUfcrDcedte) != 0u ? kUsr2Riin : 0u;
        return rx_.Usr2() | autobaud_.Usr2() | kUsr2Dcdin | riin | (tx_.Txfe() ? kUsr2Txfe : 0u) |
               (tx_.Txdc() ? kUsr2Txdc : 0u);
    }

    bool Level() const { return cerf_freescale_uart::InterruptAsserted(ctrl_, Usr1(), Usr2()); }

    uint32_t Reg(uint32_t off) {
        using namespace cerf_freescale_uart;
        if (off == kOffUrxd) {
            if (!module_clocks_->Running(kIpgModule)) return 0u;
            const uint16_t v = rx_.Pop(line_tick_);
            Sync();
            return v;
        }
        if (off == kOffUtxd) return 0u;
        if (off == kOffUsr1) return Usr1();
        if (off == kOffUsr2) return Usr2();
        if (off == kOffUbrc) return autobaud_.Ubrc();
        if (off == kOffUts) {
            uint32_t v = C(kOffUts) & kUtsWritable;
            if (tx_.Level() == 0u)         v |= kUtsTxempty;
            if (rx_.Level() == 0u)         v |= kUtsRxempty;
            if (tx_.Level() == kFifoDepth) v |= kUtsTxfull;
            if (rx_.Level() == kFifoDepth) v |= kUtsRxfull;
            return v;
        }
        if (off >= kCtrlLo && off <= kCtrlHi && (off & 3u) == 0u) {
            uint32_t v = C(off);
            if (off == kOffUcr2) v |= kUcr2Srst;
            return v;
        }
        HaltUnsupportedAccess("Read", kBase + off, 0);
    }

    void Wr(uint32_t off, uint32_t v, uint32_t shift, uint32_t vmask) {
        using namespace cerf_freescale_uart;
        const uint32_t aligned = off & ~3u;
        const uint32_t m       = vmask << shift;
        const uint32_t val     = (v << shift) & m;
        if (aligned == kOffUtxd) {
            if (!module_clocks_->Running(kIpgModule)) return;
            tx_.Write(line_tick_, static_cast<uint8_t>(v & 0xFFu));
            Sync();
            return;
        }
        if (aligned == kOffUsr1 || aligned == kOffUsr2) {
            const uint32_t usr1 = aligned == kOffUsr1 ? val : 0u, usr2 = aligned == kOffUsr2 ? val : 0u;
            rx_.ClearFlags(line_tick_, usr1, usr2);
            autobaud_.ClearFlags(usr2);
            escape_.ClearFlags(usr1);
            Sync();
            return;
        }
        if (aligned < kCtrlLo || aligned > kCtrlHi) HaltUnsupportedAccess("Write", kBase + off, v);
        if (aligned == kOffUbrc) return;
        const uint32_t regmax = aligned == kOffOnems ? kOnemsMask
                              : aligned == kOffUts  ? kUtsWritable
                              : aligned == kOffUesc ? kUescMask
                              : aligned == kOffUtim ? kUtimMask : 0xFFFFu;
        uint32_t& r = C(aligned);
        r = (r & ~(m & regmax)) | (val & regmax);
        if (aligned == kOffUcr2) r &= ~kUcr2Srst;
        if (aligned == kOffUbir) ubir_written_ = true;
        if (aligned == kOffUbmr && ubir_written_) {
            active_ubir_  = C(kOffUbir);
            active_ubmr_  = C(kOffUbmr);
            ubir_written_ = false;
        }
        if (aligned == kOffUcr2 && (m & kUcr2Srst) != 0u && (val & kUcr2Srst) == 0u) {
            SoftReset();
            return;
        }
        if (aligned == kOffUfcr || aligned == kOffUbmr) ApplyBaud();
        else                                             Configure();
    }

    void EmitTx(uint8_t ch) {
        if (endpoint_) { endpoint_->OnGuestTx(ch); return; }
        char tag[8];
        std::snprintf(tag, sizeof(tag), "UART%d", kUartNum);
        emu_.Get<KernelDebugSink>().EmitChar(static_cast<char>(ch), tx_line_, tag);
    }

    uint32_t                 ctrl_[kCtrlCount] = {};
    uint32_t                 active_ubir_   = 0;
    uint32_t                 active_ubmr_   = 0;
    bool                     ubir_written_  = false;
    uint64_t                 line_tick_     = 0;
    bool                     line_          = false;
    bool                     in_sync_       = false;
    bool                     brm_dirty_     = false;
    bool                     module_on_     = false;
    bool                     brm_running_   = false;
    bool                     held_          = false;
    FreescaleLowPowerMode    lp_mode_       = FreescaleLowPowerMode::kRun;
    uint64_t                 reply_tick_    = 0;
    std::string              tx_line_;
    std::vector<FreescaleUartTxEvent> events_;
    FreescaleUartReceiver::Events     rx_events_;
    UartEndpoint*            endpoint_      = nullptr;
    GuestCycleClock*         clock_         = nullptr;
    FreescaleModuleClocks*   module_clocks_ = nullptr;
    FreescaleTimerClocks*    timer_clocks_  = nullptr;
    FreescaleUartClocks*     uart_clocks_   = nullptr;
    FreescaleUartTransmitter tx_;
    FreescaleUartReceiver    rx_;
    FreescaleUartAutobaud    autobaud_;
    FreescaleUartEscape      escape_{emu_};
    FreescaleUartSdma        sdma_{emu_, kUartNum, tx_, rx_};
};

}
