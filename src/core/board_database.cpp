#include <cerfcore/core/board_database.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/config_json.h>
#include "share_folder_mount_name.h"

#include <nlohmann/json.hpp>

namespace cerfcore {

REGISTER_SERVICE(BoardDatabase);

namespace {

using nlohmann::json;

constexpr uint32_t kDefaultGaColorDepth = 24u;

uint32_t RequirePositiveInt(const json& obj, const char* key, const std::string& path,
                            const std::string& ctx) {
    if (!obj.contains(key) || !obj[key].is_number_integer() || obj[key].get<int>() < 1)
        CfgFatal(path, ctx + "." + key + " must be a positive integer");
    return (uint32_t)obj[key].get<int>();
}

RomPlacingMode ParseRomPlacingMode(const std::string& v, const std::string& path,
                                   const std::string& ctx) {
    if (v == "flat_container") return RomPlacingMode::FlatContainer;
    if (v == "imx51_nand")     return RomPlacingMode::Imx51Nand;
    CfgFatal(path, ctx + ".rom_placing_mode '" + v + "' is unknown");
}

SupportLevel ParseSupportLevel(const std::string& v, const std::string& path,
                               const std::string& ctx) {
    if (v == "bootable_full")    return SupportLevel::BootableFull;
    if (v == "bootable_partial") return SupportLevel::BootablePartial;
    if (v == "unknown_partial")  return SupportLevel::UnknownPartial;
    if (v == "no_support")       return SupportLevel::NoSupport;
    CfgFatal(path, ctx + ".support_level '" + v + "' is unknown");
}

const json& FindBoardFamily(const json& families, const std::string& id,
                            const std::string& path, const std::string& ctx) {
    for (const auto& f : families)
        if (f.is_object() && f.contains("id") && f["id"] == id) return f;
    CfgFatal(path, ctx + ".board_family_id '" + id + "' is not in board_families[]");
}

struct BoardValues {
    const json&        board;
    const json&        family;
    const std::string& board_ctx;
    const std::string& family_ctx;

    const json& Owner(const char* key) const {
        return board.contains(key) ? board : family;
    }
    const std::string& OwnerCtx(const char* key) const {
        return board.contains(key) ? board_ctx : family_ctx;
    }
};

}

CpuArch ParseCpuArch(const std::string& value, const std::string& path,
                     const std::string& ctx) {
    if (value == "ARM")  return CpuArch::kArm;
    if (value == "MIPS") return CpuArch::kMips;
    CfgFatal(path, ctx + ".arch '" + value + "' is unknown");
}

void BoardDatabase::OnReady() {
    const std::string path = DbJsonPath();
    json root = CfgReadInstalledJsonFile(path);

    const char* kMountPointKey = "ga_shared_folder_mount_point";
    if (!root.contains(kMountPointKey) || !root[kMountPointKey].is_string() ||
        !IsValidShareFolderMountName(root[kMountPointKey].get<std::string>()))
        CfgFatal(path, std::string("'") + kMountPointKey +
                       "' must be a valid mount point name");
    ga_shared_folder_mount_point_ = root[kMountPointKey].get<std::string>();

    for (const auto& n : CfgRequireArray(root, "ga_video_driver_names", path)) {
        if (!n.is_string() || n.get<std::string>().empty())
            CfgFatal(path, "ga_video_driver_names[] entries must be non-empty strings");
        ga_video_driver_names_.push_back(n.get<std::string>());
    }

    for (const auto& f : CfgRequireArray(root, "soc_families", path)) {
        if (!f.is_object()) CfgFatal(path, "soc_families[] entries must be objects");
        DbSocFamily row;
        row.id   = CfgRequireString(f, "id", path, "soc_families[]");
        row.name = CfgRequireString(f, "name", path, "soc_families[]");
        row.arch = ParseCpuArch(CfgRequireString(f, "arch", path, "soc_families[]"),
                                path, "soc_families[" + row.id + "]");
        soc_families_.push_back(std::move(row));
    }

    for (const auto& s : CfgRequireArray(root, "socs", path)) {
        if (!s.is_object()) CfgFatal(path, "socs[] entries must be objects");
        DbSoc row;
        row.id        = CfgRequireString(s, "id", path, "socs[]");
        row.name      = CfgRequireString(s, "name", path, "socs[]");
        row.family_id = CfgRequireString(s, "family_id", path, "socs[" + row.id + "]");
        SocFamily(row.family_id);
        socs_.push_back(std::move(row));
    }

    const json& families = CfgRequireArray(root, "board_families", path);
    for (const auto& b : CfgRequireArray(root, "boards", path)) {
        if (!b.is_object()) CfgFatal(path, "boards[] entries must be objects");
        DbBoard row;
        row.id = CfgRequireString(b, "id", path, "boards[]");
        const std::string ctx = "boards[" + row.id + "]";
        row.name       = CfgRequireString(b, "name", path, ctx);
        row.short_name = b.contains("short_name")
                             ? CfgRequireString(b, "short_name", path, ctx)
                             : row.name;
        row.family_id  = CfgRequireString(b, "board_family_id", path, ctx);
        const std::string family_ctx = "board_families[" + row.family_id + "]";
        const BoardValues v{ b, FindBoardFamily(families, row.family_id, path, ctx),
                             ctx, family_ctx };
        row.support_level = ParseSupportLevel(
            CfgRequireString(b, "support_level", path, ctx), path, ctx);
        row.soc_id = CfgRequireString(v.Owner("soc_id"), "soc_id", path,
                                      v.OwnerCtx("soc_id"));
        Soc(row.soc_id);
        if (v.Owner("lcd_panel_size").contains("lcd_panel_size")) {
            const auto& p = v.Owner("lcd_panel_size")["lcd_panel_size"];
            const std::string pctx = v.OwnerCtx("lcd_panel_size") + ".lcd_panel_size";
            if (!p.is_object()) CfgFatal(path, pctx + " must be an object");
            row.lcd_panel_size = DbLcdPanelSize{
                RequirePositiveInt(p, "width", path, pctx),
                RequirePositiveInt(p, "height", path, pctx) };
        }
        row.ga_color_depth = v.Owner("ga_color_depth").contains("ga_color_depth")
            ? RequirePositiveInt(v.Owner("ga_color_depth"), "ga_color_depth", path,
                                 v.OwnerCtx("ga_color_depth"))
            : kDefaultGaColorDepth;
        row.has_high_dpi = CfgReadOptBool(v.Owner("has_high_dpi"), "has_high_dpi", path,
                                          v.OwnerCtx("has_high_dpi"));
        row.rom_placing_mode = ParseRomPlacingMode(
            CfgRequireString(v.Owner("rom_placing_mode"), "rom_placing_mode", path,
                             v.OwnerCtx("rom_placing_mode")),
            path, v.OwnerCtx("rom_placing_mode"));
        boards_.push_back(std::move(row));
    }
}

const DbBoard* BoardDatabase::FindBoard(std::string_view id) const {
    for (const auto& b : boards_)
        if (b.id == id) return &b;
    return nullptr;
}

const DbSoc& BoardDatabase::Soc(std::string_view id) const {
    for (const auto& s : socs_)
        if (s.id == id) return s;
    CfgFatal(DbJsonPath(), "soc '" + std::string(id) + "' is not in socs[]");
}

const DbSocFamily& BoardDatabase::SocFamily(std::string_view id) const {
    for (const auto& f : soc_families_)
        if (f.id == id) return f;
    CfgFatal(DbJsonPath(),
             "soc family '" + std::string(id) + "' is not in soc_families[]");
}

}
