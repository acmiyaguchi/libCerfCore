#include "nec_mobilepro_900_series_pic_loader.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/state/state_stream.h>
#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_900_series_family_id.h"

REGISTER_SERVICE(NecMobilepro900SeriesPicLoader);

namespace {

/* nec_mobilepro_900_hpc2000 upgrdpco.exe start 0x11CB0: after 0x17 and after each fgets line a
   reply of 24 continues, 25 resends the line, and 26 ending the loop reports the download done. */
constexpr uint8_t kAccepted = 0x18u;
constexpr uint8_t kComplete = 0x1Au;

/* nec_mobilepro_900_hpc2000 upgrdpco.exe sub_11B20: leading spaces, one mark character, then LL,
   the 16-bit offset and the type. Intel HEX Spec Rev A page 5: RECTYP '01' End of File Record. */
constexpr uint32_t kTypeField   = 7u;
constexpr uint8_t  kEndOfFile[] = {'0', '1'};

}

bool NecMobilepro900SeriesPicLoader::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecMobilepro900Series;
}

uint8_t NecMobilepro900SeriesPicLoader::Enter() {
    LOG(Periph, "[PCO] firmware download mode\n");
    active_ = true;
    StartLine();
    return kAccepted;
}

void NecMobilepro900SeriesPicLoader::OnPicReset() {
    active_ = false;
    StartLine();
}

void NecMobilepro900SeriesPicLoader::StartLine() {
    line_len_ = 0u;
    field_    = 0u;
    type_     = {};
}

bool NecMobilepro900SeriesPicLoader::TakeByte(uint8_t b, uint8_t* reply) {
    if (b != '\n') {
        ++line_len_;
        if (field_ == 0u && b == ' ') return false;
        if (field_ == kTypeField || field_ == kTypeField + 1u) type_[field_ - kTypeField] = b;
        if (field_ <= kTypeField + 1u) ++field_;
        return false;
    }
    const bool end = field_ > kTypeField + 1u && type_[0] == kEndOfFile[0] &&
                     type_[1] == kEndOfFile[1];
    *reply = end ? kComplete : kAccepted;
    LOG(Periph, "[PCO] download line of %u bytes: reply 0x%02X\n", line_len_, *reply);
    StartLine();
    return true;
}

void NecMobilepro900SeriesPicLoader::SaveState(StateWriter& w) const {
    w.Write<uint8_t>("pic_dl_active", active_ ? 1u : 0u);
    w.Write<uint32_t>("pic_dl_line_len", line_len_);
    w.Write<uint32_t>("pic_dl_field", field_);
    w.WriteBytes("pic_dl_type", type_.data(), type_.size());
}

void NecMobilepro900SeriesPicLoader::RestoreState(StateReader& r) {
    uint8_t active = 0u;
    r.Read("pic_dl_active", active);
    r.Read("pic_dl_line_len", line_len_);
    r.Read("pic_dl_field", field_);
    r.ReadBytes("pic_dl_type", type_.data(), type_.size());
    active_ = active != 0u;
}
