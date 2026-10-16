#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include "pr31500_id.h"
#include "pr31700_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "pr31x00_clock.h"
#include "pr31x00_intc.h"
#include "pr31x00_sib_audio.h"
#include "pr31x00_sib_codec.h"

#include <cstdint>

namespace {

/* Philips PR31x00 Serial Interface Bus, TMPR3911/3912 ch.13. Subframe 0 carries a
   register command to an external codec; sound and telecom frames are separate. */
constexpr uint32_t kBase = 0x10C00060u;

constexpr uint32_t kOffCtl     = 0x14u;   /* $074 */
constexpr uint32_t kOffSndTx   = 0x18u;   /* $078 write-only, $078 reads SNDRXHOLD */
constexpr uint32_t kOffTelTx   = 0x1Cu;   /* $07C write-only, $07C reads TELRXHOLD */
constexpr uint32_t kOffSf0Aux  = 0x20u;   /* $080 */
constexpr uint32_t kOffSf1Aux  = 0x24u;   /* $084 */
constexpr uint32_t kOffSf0Stat = 0x28u;   /* $088 read-only */
constexpr uint32_t kOffDmaCtl  = 0x30u;   /* $090 */

/* SF0AUX (tx39sibreg.h:145-166): REGADDR[3:0]<30:27> WRITE<26> SNDVALID<17>
   TELVALID<16> REGDATA[15:0]<15:0>. */
constexpr uint32_t kSf0RegAddrShift = 27;
constexpr uint32_t kSf0RegAddrMask  = 0xFu;
constexpr uint32_t kSf0Write        = 1u << 26;
constexpr uint32_t kSf0SndValid     = 1u << 17;
constexpr uint32_t kSf0TelValid     = 1u << 16;
constexpr uint32_t kSf0RegDataMask  = 0xFFFFu;

constexpr uint32_t kCtlEnSib     = 1u << 0;
constexpr uint32_t kCtlEnSf0     = 1u << 1;
constexpr uint32_t kCtlEnSf1     = 1u << 2;
constexpr uint32_t kCtlEnSnd     = 1u << 4;
constexpr uint32_t kCtlEnTel     = 1u << 5;
constexpr uint32_t kCtlSelSndSf1 = 1u << 6;
constexpr uint32_t kCtlSelTelSf1 = 1u << 7;
constexpr uint32_t kCtlSnd16     = 1u << 15;
constexpr uint32_t kCtlTel16     = 1u << 23;
constexpr uint32_t kCtlSndMono   = 1u << 28;

/* Interrupt Status 1 bit 8 SIBSF0INT and bit 7 SIBSF1INT (§8.3.1). */
constexpr uint32_t kSibSf0Int = 1u << 8;
constexpr uint32_t kSibSf1Int = 1u << 7;

/* SNDFSDIV[6:0]<14:8>, TELFSDIV[6:0]<22:16> (§13.6.6). */
constexpr uint32_t kCtlSndFsShift = 8;
constexpr uint32_t kCtlTelFsShift = 16;
constexpr uint32_t kCtlFsMask     = 0x7Fu;

/* SIBSCLKDIV[2:0]<26:24> divide-modulus 2, 3, 4, 5, 6, 8, 10, 12 (§13.6.6 p13-22); Fs =
   (SIBSCLK x 2) / ((FSDIV + 1) x 64) (§13.3.4 p13-8). */
constexpr uint32_t kCtlSibSclkDivShift = 24;
constexpr uint32_t kCtlSibSclkDivMask  = 0x7u;
constexpr uint64_t kSibSclkModulus[8]  = {2u, 3u, 4u, 5u, 6u, 8u, 10u, 12u};

constexpr uint32_t kOffSize     = 0x00u;   /* $060 write-only */
constexpr uint32_t kOffSndRxSt  = 0x04u;   /* $064 write-only */
constexpr uint32_t kOffSndTxSt  = 0x08u;   /* $068 write-only */
constexpr uint32_t kOffTelRxSt  = 0x0Cu;   /* $06C write-only */
constexpr uint32_t kOffTelTxSt  = 0x10u;   /* $070 write-only */

/* SIB Size (§13.6.1): SNDSIZE[13:2]<29:18> TELSIZE[13:2]<13:2>; 31-30, 17-14 and
   1-0 reserved. The four Start registers (§13.6.2-§13.6.5) each carry a buffer's
   physical address in [31:2] and reserve 1-0. */
constexpr uint32_t kSizeReserved  = 0xC003C003u;
constexpr uint32_t kStartReserved = 0x00000003u;

/* Sound and telecom DMA buffers: SNDSIZE[13:2]<29:18>, TELSIZE[13:2]<13:2> (§13.6.1), byte
   length (SIZE+1)*4; SNDRXSTART, SNDTXSTART, TELRXSTART, TELTXSTART [31:2] (§13.6.2-§13.6.5). */
constexpr uint32_t kSizeSndShift   = 18;
constexpr uint32_t kSizeTelShift   = 2;
constexpr uint32_t kSizeMask       = 0xFFFu;
constexpr uint32_t kStartMask      = 0xFFFFFFFCu;

/* SIB Control (§13.6.6): SIBIRQ<31>(R) reports the SIBIRQ input pin, which the
   TC35143 drives active-high; ENSIB<0>, ENSF0<1>, ENSF1<2>, SIBLOOP<3>, ENSND<4>,
   ENTEL<5> and the two test bits all reset to 0, and the frame-rate fields reset
   undefined and feed no enabled subframe. */
constexpr uint32_t kCtlReset = 0;
constexpr uint32_t kCtlResetKept = 0x1FFFFFC0u;

constexpr uint32_t kCtlWritable = 0x7FFFFFFFu;   /* SIBIRQ<31> is read-only */
constexpr uint32_t kCtlSibIrq   = 1u << 31;      /* SIBIRQ input-pin level (§13.6.6) */

/* ENCNTTEST<30> and ENDMATEST<29> "should not be set" (§13.6.6). */
constexpr uint32_t kCtlTest    = 0x60000000u;
constexpr uint32_t kCtlRunning = 0x0000000Cu;   /* SIBLOOP<3>, ENSF1<2> */

/* SIB DMA Control (§13.6.15): SNDBUFF1TIME<31> SNDDMALOOP<30> SNDDMAPTR[13:2]<29:18>(R)
   ENDMARXSND<17> ENDMATXSND<16> TELBUFF1TIME<15> TELDMALOOP<14> TELDMAPTR[13:2]<13:2>(R)
   ENDMARXTEL<1> ENDMATXTEL<0>. */
constexpr uint32_t kDmaEnTxSnd      = 1u << 16;
constexpr uint32_t kDmaEnRxSnd      = 1u << 17;
constexpr uint32_t kDmaSndLoop      = 1u << 30;
constexpr uint32_t kDmaSndBuff1Time = 1u << 31;
constexpr uint32_t kDmaEnTxTel      = 1u << 0;
constexpr uint32_t kDmaEnRxTel      = 1u << 1;
constexpr uint32_t kDmaTelLoop      = 1u << 14;
constexpr uint32_t kDmaTelBuff1Time = 1u << 15;
constexpr uint32_t kDmaCtlReadOnly  = 0x3FFC3FFCu;
constexpr uint32_t kDmaSndPtrShift  = 18;
constexpr uint32_t kDmaTelPtrShift  = 2;

class Pr31x00Sib : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd) return false;
        const std::string_view soc = bd->GetSocId();
        return soc == SocId::Pr31500 || soc == SocId::Pr31700;
    }

    void OnReady() override {
        codec_ = &emu_.Get<Pr31x00SibCodec>();
        codec_->RegisterBeforeChange([this] {
            if (sound_active_ || telecom_active_) emu_.Get<Pr31x00SibAudioSink>().FlushReceived();
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<Pr31x00Clock>().RegisterModuleClockListener([this] { UpdateDma(); });
        /* TMPR3911 §13.6.6 p13-21 SIB Control RESET: ENCNTTEST, ENDMATEST and ENTEL..ENSIB 0,
           SNDMONO..SELSNDSF1 X; §13.6.15 p13-27 SIB DMA Control: every R/W bit 0. */
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            ctl_    &= kCtlResetKept;
            dma_ctl_ = 0;
            UpdateDma();
            UpdateSibFrameInts();
        });
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return 0x34u; }   /* $060-$093 */

    /* OEMIdle polls ENSIB<0> here before stopping the CPU (nino_300 nk.exe
       sub_9F411310 tests MEMORY[0xB0C00074] & 1 ahead of POWER_CTL STOPCPU), so
       this read must answer. SIBIRQ<31> reports the codec IRQ input pin, active-high
       (§13.6.6): reflect the modelled codec's pending interrupt. */
    uint32_t ReadWord(uint32_t addr) override {
        if (addr - kBase == kOffCtl) {
            uint32_t v = ctl_;
            if (codec_->IrqAsserted()) v |= kCtlSibIrq;
            return v;
        }
        /* SF0AUX holds the subframe-0 control word and is R/W (§13.6.11); serial.dll's
           UCB register handler sub_1EBACE8 reads it to save and restore it around a
           transaction. */
        if (addr - kBase == kOffSf0Aux) return sf0_aux_;
        if (addr - kBase == kOffSf0Stat) return sf0_stat_;
        if (addr - kBase == kOffDmaCtl) {
            auto& sink = emu_.Get<Pr31x00SibAudioSink>();
            return dma_ctl_ | (sink.DmaPointer(Pr31x00SibCircuit::Sound) << kDmaSndPtrShift) |
                   (sink.DmaPointer(Pr31x00SibCircuit::Telecom) << kDmaTelPtrShift);
        }
        HaltUnsupportedAccess("PR31x00 SIB ReadWord", addr, 0);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        if (addr - kBase == kOffDmaCtl) {
            WriteDmaCtl(value);
            return;
        }
        /* SF0AUX and SF1AUX hold the control word transmitted during their subframe,
           re-sent every frame until rewritten (§13.6.11, §13.6.12). SNDTXHOLD and
           TELTXHOLD hold the next sound and telecom sample (§13.6.7, §13.6.9). */
        if (addr - kBase == kOffSize) {
            if (value & kSizeReserved) {
                HaltUnsupportedAccess("PR31x00 SIB SIZE reserved", addr, value);
            }
            snd_size_ = (value >> kSizeSndShift) & kSizeMask;
            tel_size_ = (value >> kSizeTelShift) & kSizeMask;
            UpdateDma();
            return;
        }
        if (addr - kBase == kOffSndTxSt || addr - kBase == kOffSndRxSt ||
            addr - kBase == kOffTelRxSt || addr - kBase == kOffTelTxSt) {
            if (value & kStartReserved) {
                HaltUnsupportedAccess("PR31x00 SIB DMA start reserved", addr, value);
            }
            const uint32_t start = value & kStartMask;
            if (addr - kBase == kOffSndTxSt) snd_tx_start_ = start;
            if (addr - kBase == kOffSndRxSt) snd_rx_start_ = start;
            if (addr - kBase == kOffTelTxSt) tel_tx_start_ = start;
            if (addr - kBase == kOffTelRxSt) tel_rx_start_ = start;
            UpdateDma();
            return;
        }
        if (addr - kBase == kOffSf0Aux) { WriteSf0Aux(addr, value); return; }
        if (addr - kBase == kOffSf1Aux) { sf1_aux_ = value; return; }
        if (addr - kBase == kOffSndTx)  { snd_tx_hold_ = value; return; }
        if (addr - kBase == kOffTelTx)  { tel_tx_hold_ = value; return; }
        if (addr - kBase != kOffCtl) {
            HaltUnsupportedAccess("PR31x00 SIB WriteWord", addr, value);
        }
        if (value & kCtlTest) {
            HaltUnsupportedAccess("PR31x00 SIB CTL IC-test bit", addr, value);
        }
        if (value & kCtlRunning) {
            HaltUnsupportedAccess("PR31x00 SIB CTL sets SIBLOOP or ENSF1", addr, value);
        }
        ctl_ = value & kCtlWritable;
        UpdateDma();
        UpdateSibFrameInts();
    }

    void WriteSf0Aux(uint32_t addr, uint32_t value) {
        if (value & (kSf0SndValid | kSf0TelValid)) {
            HaltUnsupportedAccess("PR31x00 SIB SF0AUX carries a sound or telecom sample",
                                  addr, value);
        }
        sf0_aux_ = value;

        const uint8_t  reg  = static_cast<uint8_t>((value >> kSf0RegAddrShift) & kSf0RegAddrMask);
        const uint16_t data = static_cast<uint16_t>(value & kSf0RegDataMask);
        if (value & kSf0Write) codec_->WriteReg(reg, data);
        else                   sf0_stat_ = codec_->ReadReg(reg);
    }

    /* SNDDMAPTR[13:2] and TELDMAPTR[13:2] are read-only (TMPR3911 §13.6.15, printed
       p.13-27). */
    void WriteDmaCtl(uint32_t value) {
        dma_ctl_ = value & ~kDmaCtlReadOnly;
        UpdateDma();
    }

    void SaveState(StateWriter& w) override {
        w.Write("ctl", ctl_); w.Write("dma_ctl", dma_ctl_); w.Write("sf0_aux", sf0_aux_); w.Write("sf1_aux", sf1_aux_);
        w.Write("snd_tx_hold", snd_tx_hold_); w.Write("tel_tx_hold", tel_tx_hold_); w.Write("sf0_stat", sf0_stat_);
        w.Write("snd_tx_start", snd_tx_start_); w.Write("snd_rx_start", snd_rx_start_);
        w.Write("snd_size", snd_size_);
        w.Write("tel_tx_start", tel_tx_start_); w.Write("tel_rx_start", tel_rx_start_);
        w.Write("tel_size", tel_size_);
        codec_->SaveState(w);
        emu_.Get<Pr31x00SibAudioSink>().SaveState(w);
    }
    void RestoreState(StateReader& r) override {
        r.Read("ctl", ctl_); r.Read("dma_ctl", dma_ctl_); r.Read("sf0_aux", sf0_aux_); r.Read("sf1_aux", sf1_aux_);
        r.Read("snd_tx_hold", snd_tx_hold_); r.Read("tel_tx_hold", tel_tx_hold_); r.Read("sf0_stat", sf0_stat_);
        r.Read("snd_tx_start", snd_tx_start_); r.Read("snd_rx_start", snd_rx_start_);
        r.Read("snd_size", snd_size_);
        r.Read("tel_tx_start", tel_tx_start_); r.Read("tel_rx_start", tel_rx_start_);
        r.Read("tel_size", tel_size_);
        codec_->RestoreState(r);
        emu_.Get<Pr31x00SibAudioSink>().RestoreState(r);
    }
    void PostRestore() override {
        sound_active_   = SoundArmed();
        telecom_active_ = TelecomArmed();
        codec_->PostRestore();
    }

private:
    bool SoundArmed() const {
        return (ctl_ & kCtlEnSib) && (ctl_ & kCtlEnSnd) && (dma_ctl_ & (kDmaEnTxSnd | kDmaEnRxSnd));
    }
    /* ENDMARXTEL / ENDMATXTEL "should not be set until ... the SIB module is enabled (ENSIB
       asserted) and ENTEL is set" (TMPR3911 §13.6.15, printed p.13-29). */
    bool TelecomArmed() const {
        return (ctl_ & kCtlEnSib) && (ctl_ & kCtlEnTel) && (dma_ctl_ & (kDmaEnTxTel | kDmaEnRxTel));
    }
    /* TELFSDIV: "9.216 MHz ÷ (40 × 32) = 7.2 kHz" for TELFSDIV 39 (§13.6.6, printed p.13-22),
       the sound law with TELFSDIV in place of SNDFSDIV. */
    GuestCycleClock::Rate SampleRate(uint32_t fs_shift) const {
        const uint32_t fsdiv   = (ctl_ >> fs_shift) & kCtlFsMask;
        const uint64_t modulus = kSibSclkModulus[(ctl_ >> kCtlSibSclkDivShift) & kCtlSibSclkDivMask];
        const GuestCycleClock::Rate mclk = emu_.Get<Pr31x00Clock>().SibMasterClockRate();
        if (mclk.num == 0u) return GuestCycleClock::Rate{0u, 1u};
        return mclk.Scaled(2u, static_cast<uint64_t>(modulus) * (fsdiv + 1u) * 64u);
    }

    /* SNDMONO, SND16, SELSNDSF1, ENSF1, ENSF0 (TMPR3911 §13.6.6, printed p.13-21 to 13-24). */
    Pr31x00SibDmaSetup SoundDma() const {
        Pr31x00SibDmaSetup dma;
        const bool sf1 = (ctl_ & kCtlSelSndSf1) != 0u;
        dma.tx_start_pa = snd_tx_start_;
        dma.rx_start_pa = snd_rx_start_;
        dma.size        = snd_size_;
        dma.tx          = (dma_ctl_ & kDmaEnTxSnd) != 0u;
        dma.rx          = (dma_ctl_ & kDmaEnRxSnd) != 0u;
        dma.rx_first    = (dma_ctl_ & kDmaSndLoop) != 0u;
        dma.one_time    = (dma_ctl_ & kDmaSndBuff1Time) != 0u;
        dma.sixteen_bit = (ctl_ & kCtlSnd16) != 0u;
        dma.stereo      = sf1 && (ctl_ & kCtlSndMono) == 0u;
        dma.muted       = (ctl_ & (sf1 ? kCtlEnSf1 : kCtlEnSf0)) == 0u;
        dma.frame_rate  = SampleRate(kCtlSndFsShift);
        return dma;
    }

    /* TEL16, SELTELSF1 (§13.6.6); TELBUFF1TIME, TELDMALOOP (§13.6.15, printed p.13-28); telecom
       is mono in both subframes (Tables 13.3.4 / 13.3.6). */
    Pr31x00SibDmaSetup TelecomDma() const {
        Pr31x00SibDmaSetup dma;
        const bool sf1 = (ctl_ & kCtlSelTelSf1) != 0u;
        dma.tx_start_pa = tel_tx_start_;
        dma.rx_start_pa = tel_rx_start_;
        dma.size        = tel_size_;
        dma.tx          = (dma_ctl_ & kDmaEnTxTel) != 0u;
        dma.rx          = (dma_ctl_ & kDmaEnRxTel) != 0u;
        dma.rx_first    = (dma_ctl_ & kDmaTelLoop) != 0u;
        dma.one_time    = (dma_ctl_ & kDmaTelBuff1Time) != 0u;
        dma.sixteen_bit = (ctl_ & kCtlTel16) != 0u;
        dma.stereo      = false;
        dma.muted       = (ctl_ & (sf1 ? kCtlEnSf1 : kCtlEnSf0)) == 0u;
        dma.frame_rate  = SampleRate(kCtlTelFsShift);
        return dma;
    }

    void RequireSeparateOrSharedBuffers(const Pr31x00SibDmaSetup& dma) const {
        if (!dma.rx || !dma.tx || dma.rx_start_pa == dma.tx_start_pa) return;
        const uint64_t bytes = (static_cast<uint64_t>(dma.size) + 1u) * 4u;
        if (dma.rx_start_pa < dma.tx_start_pa + bytes && dma.tx_start_pa < dma.rx_start_pa + bytes) {
            emu_.Get<Fatal>().Die("Pr31x00Sib: sound RX buffer 0x%08X and TX buffer 0x%08X of "
                                  "%llu bytes overlap", dma.rx_start_pa, dma.tx_start_pa,
                                  static_cast<unsigned long long>(bytes));
        }
    }

    void UpdateCircuit(Pr31x00SibCircuit circuit, bool armed, const Pr31x00SibDmaSetup& dma,
                       bool& active) {
        auto& sink = emu_.Get<Pr31x00SibAudioSink>();
        if (armed) {
            if (active) {
                sink.UpdateDma(circuit, dma);
            } else {
                sink.StartDma(circuit, dma);
                active = true;
            }
        } else if (active) {
            sink.StopDma(circuit);
            active = false;
        }
    }

    void UpdateDma() {
        const bool sound_armed   = SoundArmed();
        const bool telecom_armed = TelecomArmed();
        const Pr31x00SibDmaSetup sound = sound_armed ? SoundDma() : Pr31x00SibDmaSetup{};
        if (sound_armed) RequireSeparateOrSharedBuffers(sound);
        UpdateCircuit(Pr31x00SibCircuit::Sound, sound_armed, sound, sound_active_);
        UpdateCircuit(Pr31x00SibCircuit::Telecom, telecom_armed,
                      telecom_armed ? TelecomDma() : Pr31x00SibDmaSetup{}, telecom_active_);
    }

    /* Every SIB frame carries both subframe 0 and subframe 1 (§13.3.1 Fig 13.3.1), so
       SIBSF0INT and SIBSF1INT both re-issue each frame while it runs; §8.3.1: SF1INT
       gates the CPU read of SF0STAT that the codec-register read protocol waits on. */
    void UpdateSibFrameInts() {
        const bool active = (ctl_ & (kCtlEnSib | kCtlEnSf0)) == (kCtlEnSib | kCtlEnSf0);
        emu_.Get<Pr31x00Intc>().SetSourceFreeRunning(0, kSibSf0Int | kSibSf1Int, active);
    }

    Pr31x00SibCodec* codec_ = nullptr;
    uint32_t ctl_     = kCtlReset;
    uint32_t dma_ctl_ = 0;
    uint32_t sf0_aux_ = 0;
    uint32_t sf1_aux_ = 0;
    uint32_t snd_tx_hold_ = 0;
    uint32_t tel_tx_hold_ = 0;
    uint32_t sf0_stat_    = 0;
    uint32_t snd_tx_start_ = 0;
    uint32_t snd_rx_start_ = 0;
    uint32_t snd_size_      = 0;
    uint32_t tel_tx_start_  = 0;
    uint32_t tel_rx_start_  = 0;
    uint32_t tel_size_      = 0;
    bool     sound_active_   = false;
    bool     telecom_active_ = false;
};

}  /* namespace */

REGISTER_SERVICE(Pr31x00Sib);
