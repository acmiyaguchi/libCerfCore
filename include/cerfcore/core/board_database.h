#pragma once

#include "service.h"
#include <cerfengine/isa.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using CpuArch = cerfengine::Isa;

CpuArch ParseCpuArch(const std::string& value, const std::string& path,
                     const std::string& ctx);

enum class RomPlacingMode { FlatContainer, Imx51Nand, Unknown };

enum class SupportLevel { BootableFull, BootablePartial, UnknownPartial, NoSupport };

struct DbLcdPanelSize { uint32_t width; uint32_t height; };

struct DbSocFamily {
    std::string id;
    std::string name;
    CpuArch     arch;
};

struct DbSoc {
    std::string id;
    std::string name;
    std::string family_id;
};

struct DbBoard {
    std::string                   id;
    std::string                   name;
    std::string                   short_name;
    std::string                   family_id;
    SupportLevel                  support_level;
    std::string                   soc_id;
    std::optional<DbLcdPanelSize> lcd_panel_size;
    uint32_t                      ga_color_depth;
    RomPlacingMode                rom_placing_mode;
};

class BoardDatabase : public Service {
public:
    using Service::Service;

    void OnReady() override;

    std::span<const DbBoard> Boards() const { return boards_; }

    const std::string& GaSharedFolderMountPoint() const {
        return ga_shared_folder_mount_point_;
    }

    std::span<const std::string> GaVideoDriverNames() const {
        return ga_video_driver_names_;
    }

    const DbBoard*     FindBoard(std::string_view id) const;
    const DbSoc&       Soc(std::string_view id) const;
    const DbSocFamily& SocFamily(std::string_view id) const;

private:
    std::vector<DbSocFamily> soc_families_;
    std::vector<DbSoc>       socs_;
    std::vector<DbBoard>     boards_;
    std::string              ga_shared_folder_mount_point_;
    std::vector<std::string> ga_video_driver_names_;
};
