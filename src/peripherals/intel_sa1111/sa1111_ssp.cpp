#include "sa1111_unit.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "../../boards/hp_jornada_700/hp_jornada_700_family_id.h"
#include <cerfcore/state/state_stream.h>
#include "sa1111_intc.h"
#include "sa1111_ssp_device.h"
#include "sa1111_system_controller.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* Table 8-3 (printed 8-9): DSS 3:0, FRF 5:4, bit 6 reserved, SSP_EN 7, SCR 15:8. Table 8-4
   (printed 8-12): LBM 2, SPO 3, SPH 4, TFT 10:7, RFT 14:11, "writes to reserved bits are
   ignored, and reads of these bits return zero". */
constexpr uint32_t kCr0Defined = 0xFFBFu;
constexpr uint32_t kCr0Sse     = 1u << 7;
constexpr uint32_t kCr1Lbm     = 1u << 2;

/* Table 8-4 (page 8-12): bits 1:0 "Reserved"; §8.6.2.1 (page 8-9) and §8.6.2.2 (page 8-10)
   describe RIM and TIM and give no bit position. */
constexpr uint32_t kCr1Defined = 0x7F9Cu;

/* Table 8-6 (page 8-15): TNF 2, RNE 3, BSY 4, TFS 5, RFS 6, ROR 7, TFL 11:8, RFL 15:12. */
constexpr uint32_t kSrTnf = 1u << 2;
constexpr uint32_t kSrRne = 1u << 3;
constexpr uint32_t kSrBsy = 1u << 4;
constexpr uint32_t kSrTfs = 1u << 5;
constexpr uint32_t kSrRfs = 1u << 6;
constexpr uint32_t kSrRor = 1u << 7;

/* §8.2.1 (page 8-2): the buffer "is seen as a block of sixteen 16-bit locations". */
constexpr uint32_t kFifoDepth = 16u;

/* Table 11-1 (printed 11-3): 24 SspXmtint, 25 SspRcvint, 26 SspROR. */
constexpr uint8_t kSrcXmt = 24u;
constexpr uint8_t kSrcRcv = 25u;
constexpr uint8_t kSrcRor = 26u;

struct WordFifo {
    uint16_t word[kFifoDepth] = {};
    uint32_t head             = 0;
    uint32_t count            = 0;

    bool     Full() const { return count == kFifoDepth; }
    void     Push(uint16_t v) { word[(head + count) % kFifoDepth] = v; ++count; }
    uint16_t Pop() {
        const uint16_t v = word[head];
        head             = (head + 1u) % kFifoDepth;
        --count;
        return v;
    }
    void Clear() { head = count = 0u; }

    struct Keys {
        const char* words;
        const char* head;
        const char* count;
    };

    template <typename F>
    static constexpr void Visit(WordFifo& q, F& field, const Keys& keys) {
        field(keys.words, q.word);
        field(keys.head, q.head);
        field(keys.count, q.count);
    }
};

constexpr WordFifo::Keys kTxFifoKeys{"tx_fifo_words", "tx_fifo_head", "tx_fifo_count"};
constexpr WordFifo::Keys kRxFifoKeys{"rx_fifo_words", "rx_fifo_head", "rx_fifo_count"};

struct Lines {
    bool xmt = false;
    bool rcv = false;
    bool ror = false;
    bool operator!=(const Lines& o) const { return xmt != o.xmt || rcv != o.rcv || ror != o.ror; }
};

class Sa1111Ssp : public Sa1111Unit {
public:
    using Sa1111Unit::Sa1111Unit;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
    }

    uint32_t MmioBase() const override { return 0x40000800u; }
    uint32_t MmioSize() const override { return 0x00000200u; }

    void SaveState(StateWriter& w) override {
        static_assert(StateVisitCoversAllBytes<WordFifo>(
                          [](WordFifo& q, StateFieldBytes& f) {
                              WordFifo::Visit(q, f, kTxFifoKeys);
                          }),
                      "WordFifo::Visit must name or skip every field of WordFifo");
        w.Write("cr0", cr0_);
        w.Write("cr1", cr1_);
        w.Write("ror", ror_);
        w.Write("rd", rd_);
        StateWriteField field(w);
        WordFifo::Visit(tx_, field, kTxFifoKeys);
        WordFifo::Visit(rx_, field, kRxFifoKeys);
        device_->SaveState(w);
    }
    void RestoreState(StateReader& r) override {
        r.Read("cr0", cr0_);
        r.Read("cr1", cr1_);
        r.Read("ror", ror_);
        r.Read("rd", rd_);
        StateReadField field(r);
        WordFifo::Visit(tx_, field, kTxFifoKeys);
        WordFifo::Visit(rx_, field, kRxFifoKeys);
        device_->RestoreState(r);
    }
    void PostRestore() override {
        clocked_ = ClockRunning();
        pub_     = CurrentLines();
    }

protected:
    void OnUnitReady() override {
        device_ = &emu_.Get<Sa1111SspDevice>();
        intc_   = &emu_.Get<Sa1111Intc>();
        emu_.Get<Sa1111SystemController>().RegisterClockListener([this] { OnClockChange(); });
        clocked_ = ClockRunning();
    }

    uint32_t UnitReadWord(uint32_t addr) override {
        const uint32_t value = ReadReg(addr - MmioBase());
        Drive();
        return value;
    }
    uint8_t UnitReadByte(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        return (uint8_t)(UnitReadWord(MmioBase() + (off & ~0x3u)) >> ((off & 0x3u) * 8u));
    }
    uint16_t UnitReadHalf(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        return (uint16_t)(UnitReadWord(MmioBase() + (off & ~0x3u)) >> ((off & 0x2u) * 8u));
    }

    void UnitWriteWord(uint32_t addr, uint32_t value) override {
        WriteReg(addr - MmioBase(), value);
        Drive();
    }
    void UnitWriteByte(uint32_t addr, uint8_t value) override {
        const uint32_t off = addr - MmioBase();
        if ((off & ~0x3u) == 0x40u) {
            WriteData(value);
        } else {
            const uint32_t shift = (off & 0x3u) * 8u;
            const uint32_t cur   = ReadReg(off & ~0x3u);
            WriteReg(off & ~0x3u, (cur & ~(0xFFu << shift)) | ((uint32_t)value << shift));
        }
        Drive();
    }

private:
    bool Sse() const { return (cr0_ & kCr0Sse) != 0u; }

    /* §8.6.1.3 (page 8-8): SSE "is the only control bit within the SSP which is reset to a known
       state"; Table 8-5 (page 8-13): "both FIFOs are cleared when the block is reset". */
    void OnChipReset(bool held) override {
        if (!held) return;
        cr0_ &= ~kCr0Sse;
        Abort();
        ror_ = false;
        device_->EndTransaction();
        Drive();
    }

    /* §8.6.4.4 / §8.6.4.5 (page 8-14) and §8.6.2.6 / §8.6.2.7 (page 8-11): TFS at or below the
       threshold, RFS at or above it, each "programmed to the threshold value minus one". */
    Lines LinesFor(uint32_t tx, uint32_t rx, bool ror) const {
        Lines l;
        l.xmt = Sse() && tx <= ((cr1_ >> 7) & 0xFu) + 1u;
        l.rcv = Sse() && rx >= ((cr1_ >> 11) & 0xFu) + 1u;
        l.ror = ror;
        return l;
    }
    Lines CurrentLines() const { return LinesFor(tx_.count, rx_.count, ror_); }

    void Drive() {
        const Lines next = CurrentLines();
        if (next.xmt != pub_.xmt) intc_->SetSourceLevel(kSrcXmt, next.xmt);
        if (next.rcv != pub_.rcv) intc_->SetSourceLevel(kSrcRcv, next.rcv);
        if (next.ror != pub_.ror) intc_->SetSourceLevel(kSrcRor, next.ror);
        pub_ = next;
    }

    uint32_t Status() const {
        uint32_t sr = ((tx_.count & 0xFu) << 8) | ((rx_.count & 0xFu) << 12);
        const Lines l = CurrentLines();
        if (!tx_.Full())                sr |= kSrTnf;
        if (rx_.count != 0u)            sr |= kSrRne;
        if (Sse() && tx_.count != 0u)   sr |= kSrBsy;
        if (l.xmt)                      sr |= kSrTfs;
        if (l.rcv)                      sr |= kSrRfs;
        if (ror_)                       sr |= kSrRor;
        return sr;
    }

    uint32_t ReadReg(uint32_t off) {
        switch (off) {
            case 0x00: return cr0_;
            case 0x04: return cr1_;
            case 0x10: return Status();
            case 0x40:
                if (rx_.count != 0u) rd_ = rx_.Pop();
                return rd_;
        }
        HaltUnsupportedAccess("ReadReg", MmioBase() + off, 0);
    }

    /* §8.6.4.8 (page 8-14): "Writes to TNF, RNE, BSY, TFS, and RFS have no effect"; §8.6.4.6:
       "Writing a one to this bit resets ROR status". */
    void WriteReg(uint32_t off, uint32_t value) {
        switch (off) {
            case 0x00: WriteCr0(value); return;
            case 0x04: cr1_ = value & kCr1Defined; return;
            case 0x10:
                if ((value & kSrRor) != 0u) ror_ = false;
                return;
            case 0x40: WriteData(value); return;
        }
        HaltUnsupportedAccess("WriteReg", MmioBase() + off, value);
    }

    /* §8.6.1.3 (page 8-8): "When the SSE bit is cleared during active operation, the SSP is
       disabled immediately", "Clearing SSE also resets the SSP's FIFOs." */
    void WriteCr0(uint32_t value) {
        cr0_ = value & kCr0Defined;
        if (!Sse()) Abort();
        device_->EndTransaction();
    }

    /* §8.6.3 (page 8-12): data shifts into the FIFO "unless it's an attempted write to a full
       Transmit FIFO". */
    void WriteData(uint32_t value) {
        if (!Sse() || tx_.Full()) return;
        tx_.Push(static_cast<uint16_t>(value));
        Settle();
    }

    void Abort() {
        tx_.Clear();
        rx_.Clear();
    }

    void Settle() {
        if (!clocked_ || !Sse()) return;
        while (tx_.count != 0u) Land(Exchange(tx_.Pop()));
    }

    /* §8.6.4.6 (page 8-14): a word received into a full FIFO sets ROR "and the newly received data
       is discarded". */
    void Land(uint16_t word) {
        if (rx_.Full()) {
            ror_ = true;
            return;
        }
        rx_.Push(word);
    }

    uint16_t Exchange(uint16_t tx) {
        const uint32_t bits = (cr0_ & 0xFu) + 1u;
        const uint32_t mask = (1u << bits) - 1u;
        /* §8.6.2.3 (page 8-10): "the output of the transmit serial shifter is directly connected
           to the input of the receive serial shifter internally". */
        if ((cr1_ & kCr1Lbm) != 0u) return static_cast<uint16_t>(tx & mask);
        /* §8.3.1.3 (page 8-5): an "8-bit command word sent from controller to peripheral", then
           "One bit-period after the last command bit, the peripheral returns the serial data". */
        if (((cr0_ >> 4) & 0x3u) == 2u) {
            device_->Shift(tx & 0xFFu, 8u);
            const uint32_t hold = (tx & 1u) != 0u ? 0xFFFFFFFFu : 0u;
            return static_cast<uint16_t>(device_->Shift(hold, bits + 1u) & mask);
        }
        return static_cast<uint16_t>(device_->Shift(tx & mask, bits));
    }

    void OnClockChange() {
        clocked_ = ClockRunning();
        Settle();
        Drive();
    }

    /* SA-1111 Developer's Manual §5.2.1 SCLKEn "SSP (Serial Port) Controller Clock"; §8.6.1.4:
       "The serial clock generator uses the 3.6864 MHz clock produced by the on-chip PLL". */
    bool ClockRunning() const { return emu_.Get<Sa1111SystemController>().SspClockRunning(); }

    Sa1111Intc*      intc_    = nullptr;
    Sa1111SspDevice* device_  = nullptr;
    Lines            pub_;
    WordFifo         tx_;
    WordFifo         rx_;
    uint32_t         cr0_     = 0;
    uint32_t         cr1_     = 0;
    uint16_t         rd_      = 0;
    bool             ror_     = false;
    bool             clocked_ = false;
};

}

REGISTER_SERVICE(Sa1111Ssp);

}
