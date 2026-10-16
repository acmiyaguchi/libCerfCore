#include "nec_mobilepro_900_series_l1110.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_900_series_family_id.h"
#include "nec_mobilepro_900_series_pcmcia.h"

bool NecMobilepro900SeriesL1110::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecMobilepro900Series;
}

void NecMobilepro900SeriesL1110::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint16_t NecMobilepro900SeriesL1110::Prs() const {
    /* PRS bits 9/10 are the PDD's card-present signal (sub_1B038D8 → SS_POWERON,
       clear = present): SET them for an empty socket or the IST loads a phantom
       card. A present card also reports BVD good (0x10/0x20); SSP (0x100) + power
       (0-3) echo. */
    uint16_t s = prc_ & 0x010Fu;
    if (emu_.Get<NecMobilepro900SeriesPcmcia>().SocketHasCard(Socket()))
        s |= 0x0010u | 0x0020u;
    else
        s |= 0x0600u;
    return s;
}

void NecMobilepro900SeriesL1110::WriteCommand(uint16_t value) {
    const uint16_t old = prc_;
    prc_ = value & 0x01FFu;                       /* PRC is 9 bits (0-8). */
    if ((value & 0x10u) && !(old & 0x10u))        /* bit 4 = card RESET, pulse. */
        emu_.Get<NecMobilepro900SeriesPcmcia>().ResetSocket(Socket());
}

/* Single 16-bit register aliased across the decode window. */
uint16_t NecMobilepro900SeriesL1110::ReadHalf(uint32_t) { return Prs(); }
uint32_t NecMobilepro900SeriesL1110::ReadWord(uint32_t) { return Prs(); }
uint8_t  NecMobilepro900SeriesL1110::ReadByte(uint32_t addr) {
    const uint16_t s = Prs();
    return static_cast<uint8_t>((addr & 1u) ? (s >> 8) : (s & 0xFFu));
}

void NecMobilepro900SeriesL1110::WriteHalf(uint32_t, uint16_t value) {
    WriteCommand(value);
}
void NecMobilepro900SeriesL1110::WriteWord(uint32_t, uint32_t value) {
    WriteCommand(static_cast<uint16_t>(value & 0xFFFFu));
}
void NecMobilepro900SeriesL1110::WriteByte(uint32_t addr, uint8_t value) {
    const uint16_t v = (addr & 1u)
        ? static_cast<uint16_t>((prc_ & 0x00FFu) | (static_cast<uint16_t>(value) << 8))
        : static_cast<uint16_t>((prc_ & 0xFF00u) | value);
    WriteCommand(v);
}

void NecMobilepro900SeriesL1110::SaveState(StateWriter& w)   { w.Write("prc", prc_); }
void NecMobilepro900SeriesL1110::RestoreState(StateReader& r) { r.Read("prc", prc_); }
