#include <cerfcore/boards/board_context.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/log.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_addr_map.h>

namespace cerfcore {

namespace {

constexpr const char* kUnknownBoard = "Unknown / unsupported";

}

bool BoardContext::ShouldRegister() {
    const DbBoard* board =
        emu_.Get<BoardDatabase>().FindBoard(emu_.Get<DeviceConfig>().board_id);
    return board && board->family_id == GetBoardFamilyId();
}

void BoardContext::OnReady() {
    if (GetBoardFamilyId().empty()) {
        LOG(Board, "board: %s\n", kUnknownBoard);
        return;
    }
    auto& db    = emu_.Get<BoardDatabase>();
    board_      = db.FindBoard(emu_.Get<DeviceConfig>().board_id);
    soc_        = &db.Soc(board_->soc_id);
    soc_family_ = &db.SocFamily(soc_->family_id);
    LOG(Board, "board: %s (family %s, SoC %s)\n", board_->name.c_str(),
        board_->family_id.c_str(), soc_->name.c_str());
}

std::string_view BoardContext::GetBoardId() const {
    return board_ ? std::string_view(board_->id) : std::string_view();
}

std::string_view BoardContext::GetSocId() const {
    return soc_ ? std::string_view(soc_->id) : std::string_view();
}

CpuArch BoardContext::GetCpuArch() const {
    return soc_family_ ? soc_family_->arch : CpuArch::kArm;
}

RomPlacingMode BoardContext::GetRomPlacingMode() const {
    return board_ ? board_->rom_placing_mode : RomPlacingMode::Unknown;
}

const char* BoardContext::BoardName() const {
    return board_ ? board_->name.c_str() : kUnknownBoard;
}

const char* BoardContext::ShortBoardName() const {
    return board_ ? board_->short_name.c_str() : kUnknownBoard;
}

const char* BoardContext::SocName() const {
    return soc_ ? soc_->name.c_str() : "";
}

uint32_t BoardContext::GuestAdditionsWindowSize() const {
    return CerfVirt::kTotalSize;
}

uint32_t BoardContext::ResolveGuestAdditionsColorDepth() const {
    const uint32_t configured =
        emu_.Get<DeviceConfig>().board_configurable_screen_bpp;
    if (configured != 0u) return configured;
    return board_->ga_color_depth;
}

bool BoardContext::HasHighDpi() const {
    return board_ && board_->has_high_dpi;
}

}
