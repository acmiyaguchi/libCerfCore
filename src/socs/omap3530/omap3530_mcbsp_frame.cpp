#include "omap3530_mcbsp_frame.h"

namespace {

/* OMAP35x TRM SPRUF98Y Table 21-58 XCR1 / Table 21-54 RCR1: FRLEN1 [14:8], WDLEN1 [7:5]; Table 21-56
   XCR2 / Table 21-52 RCR2: PHASE [15], FRLEN2 [14:8], WDLEN2 [7:5] (6 and 7 reserved), REVERSE [4:3]
   (2 and 3 reserved), DATDLY [1:0] (3 reserved); a dual-phase frame has one word in each phase. */
constexpr uint32_t kPhase = 1u << 15;

uint32_t FrameLength(uint32_t cr) { return (cr >> 8) & 0x7Fu; }
uint32_t WordCode(uint32_t cr) { return (cr >> 5) & 7u; }

uint32_t WordBits(uint32_t code) {
    static constexpr uint32_t kBits[] = {8u, 12u, 16u, 20u, 24u, 32u};
    return kBits[code];
}

/* SPRUF98Y section 21.4.6.6 (page 3016) and Figure 21-48 (page 3017): two partitions alternate A, B every
   16 channels from the frame start; Figure 21-47 (page 3016): eight partitions A-H are channels 0-127. */
bool Selected(const uint32_t (&cer)[8], bool eight_partitions, uint32_t slot) {
    const uint32_t reg = eight_partitions ? slot / 16u : (slot / 16u) % 2u;
    return ((cer[reg] >> (slot % 16u)) & 1u) != 0u;
}

struct Text {
    const char* delay;
    const char* reverse;
    const char* word;
    const char* dual;
    const char* multichannel;
};

constexpr Text kTx = {"XCR2 selects reserved XDATDLY 3", "XCR2 selects a reserved XREVERSE",
                      "XCR1 / XCR2 select a reserved word length",
                      "a dual-phase transmit frame with more than one word in a phase is reserved",
                      "a transmit multichannel selection mode with a dual-phase frame"};
constexpr Text kRx = {"RCR2 selects reserved RDATDLY 3", "RCR2 selects a reserved RREVERSE",
                      "RCR1 / RCR2 select a reserved word length",
                      "a dual-phase receive frame with more than one word in a phase is reserved",
                      "a receive multichannel selection mode with a dual-phase frame"};

}

/* SPRUF98Y page 2993: XREVERSE / RREVERSE 0b01 reverse the bit ordering of the data words (LSB first). */
uint32_t Omap3530McBspFrame::Reversed(uint32_t word, uint32_t bits) {
    uint32_t r = 0u;
    for (uint32_t b = 0; b < bits; ++b) r |= ((word >> b) & 1u) << (bits - 1u - b);
    return r;
}

/* SPRUF98Y Table 21-64 MCR2 XMCM [1:0], XMCME [9]; Table 21-22 (page 3017): 1 disabled unless selected,
   2 enabled and masked unless selected, 3 enabled per the receive channel registers and unmasked per XCER;
   page 3018: a multichannel mode needs a single-phase frame and XMCM 3 equal receive frame fields. */
const char* Omap3530McBspFrame::Build(const Regs& r, Omap3530McBspDir dir) {
    slots_.clear();
    takes_.clear();
    const bool     rx      = dir == Omap3530McBspDir::Rx;
    const Text&    text    = rx ? kRx : kTx;
    const uint32_t cr1     = rx ? r.rcr1 : r.xcr1;
    const uint32_t cr2     = rx ? r.rcr2 : r.xcr2;
    const bool     dual    = (cr2 & kPhase) != 0u;
    const uint32_t reverse = (cr2 >> 3) & 3u;
    const uint32_t mcm     = rx ? r.mcr1 & 1u : r.mcr2 & 3u;
    delay_ = cr2 & 3u;
    if (delay_ == 3u) return text.delay;
    if (reverse > 1u) return text.reverse;
    reverse_ = reverse == 1u;
    if (WordCode(cr1) > 5u || (dual && WordCode(cr2) > 5u)) return text.word;
    if (dual && (FrameLength(cr1) != 0u || FrameLength(cr2) != 0u)) return text.dual;
    if (mcm != 0u && dual) return text.multichannel;
    if (!rx && mcm == 3u && ((r.rcr2 & kPhase) != (r.xcr2 & kPhase) || FrameLength(r.rcr1) != FrameLength(r.xcr1) ||
                             WordCode(r.rcr1) != WordCode(r.xcr1) || ((r.mcr1 ^ r.mcr2) & (1u << 9)) != 0u)) {
        return "XMCM 3 with receive frame fields that differ from the transmit fields";
    }
    const uint32_t words = dual ? 2u : FrameLength(cr1) + 1u;
    const bool     xcer8 = ((r.mcr2 >> 9) & 1u) != 0u;
    const bool     rcer8 = ((r.mcr1 >> 9) & 1u) != 0u;
    uint32_t       at    = delay_;
    for (uint32_t s = 0; s < words; ++s) {
        Slot slot;
        slot.start = at;
        slot.bits  = WordBits(WordCode(dual && s == 1u ? cr2 : cr1));
        if (rx) {
            slot.kind = mcm == 0u || Selected(r.rcer, rcer8, s) ? Kind::Driven : Kind::Disabled;
        } else if (mcm == 1u) {
            slot.kind = Selected(r.xcer, xcer8, s) ? Kind::Driven : Kind::Disabled;
        } else if (mcm == 2u) {
            slot.kind = Selected(r.xcer, xcer8, s) ? Kind::Driven : Kind::Masked;
        } else if (mcm == 3u) {
            slot.kind = !Selected(r.rcer, xcer8, s) ? Kind::Disabled
                      : Selected(r.xcer, xcer8, s)  ? Kind::Driven
                                                    : Kind::Masked;
        }
        if (slot.kind != Kind::Disabled) takes_.push_back(s);
        slots_.push_back(slot);
        at += slot.bits;
    }
    data_bits_ = at - delay_;
    return nullptr;
}
