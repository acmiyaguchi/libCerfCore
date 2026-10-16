#include "compactflash_insert_entry.h"

#include "compactflash_card.h"
#include "compactflash_fat16.h"
#include "compactflash_fat32.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/host_fs_path.h>
#include <cerfcore/host/file_chooser.h>
#include <cerfcore/host/number_prompt.h>
#include <cerfcore/platform/host_file.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <system_error>
#include <vector>

REGISTER_SERVICE(CompactFlashInsertEntry);

std::string CompactFlashInsertEntry::CfImgPath() const {
    return GetCerfDir() + "CF.IMG";
}

std::string CompactFlashInsertEntry::ChooseImageFile() const {
    return emu_.Get<FileChooser>().ChooseFile(
        "Choose CompactFlash image",
        {{"CF image (*.img;*.bin;*.ima)", "*.img;*.bin;*.ima"}, {"All files", "*.*"}});
}

std::vector<std::string> CompactFlashInsertEntry::ChooseFilesToGenerate() const {
    return emu_.Get<FileChooser>().ChooseFiles(
        "Choose files to write into the CompactFlash image", {{"All files", "*.*"}});
}

uint32_t CompactFlashInsertEntry::PayloadMb(const std::vector<std::string>& files) const {
    uint64_t total = 0;
    for (const auto& f : files) {
        std::error_code ec;
        const auto size = std::filesystem::file_size(HostFsPath(f), ec);
        if (!ec) total += size;
    }
    return static_cast<uint32_t>((total + (1u << 20) - 1) >> 20);
}

uint32_t CompactFlashInsertEntry::PromptSizeMb(uint32_t min_mb) const {
    return emu_.Get<NumberPrompt>().AskAtLeast(
        "Generate CompactFlash image",
        "Card data size in MiB (minimum " + std::to_string(min_mb) + "):", min_mb);
}

SlotEntry CompactFlashInsertEntry::BuildInsertEntry(PcmciaCardCatalog::CardInserter inserter) {
    SlotEntry entry;
    auto& items = entry.variants;

    const auto& cfg = emu_.Get<DeviceConfig>();
    for (const auto& card : cfg.bundled_compact_flash_cards) {
        const std::string path = GetDeviceDir(cfg.device_name) + card.file;
        if (!host_platform::HostPathExists(path)) continue;
        SlotEntry it;
        it.id     = "bundled:" + card.file;
        it.label  = "Insert - " + card.name;
        it.insert = [this, inserter, path] {
            inserter(std::make_unique<CompactFlashCard>(emu_, path));
        };
        items.push_back(std::move(it));
    }

    {
        SlotEntry it;
        it.id    = "choose";
        it.label = "Choose image...";
        it.insert = [this, inserter] {
            const std::string p = ChooseImageFile();
            if (!p.empty())
                inserter(std::make_unique<CompactFlashCard>(emu_, p));
        };
        items.push_back(std::move(it));
    }
    {
        SlotEntry it;
        const std::string cf = CfImgPath();
        it.id      = "default";
        it.label   = "Choose default image (CF.IMG)";
        it.enabled = host_platform::HostPathExists(cf);
        it.insert  = [this, inserter, cf] {
            inserter(std::make_unique<CompactFlashCard>(emu_, cf));
        };
        items.push_back(std::move(it));
    }
    {
        SlotEntry it;
        it.id    = "generate_fat16";
        it.label = "Generate FAT16 into CF.IMG and choose it...";
        it.insert = [this, inserter] {
            const std::vector<std::string> files = ChooseFilesToGenerate();
            if (files.empty()) return;
            const uint32_t min_mb = std::max<uint32_t>(PayloadMb(files), 4u);
            const uint32_t size_mb = PromptSizeMb(min_mb);
            if (size_mb == 0) return;
            const std::string cf = CfImgPath();
            if (emu_.Get<CompactFlashFat16Builder>().Build(cf, files, size_mb))
                inserter(std::make_unique<CompactFlashCard>(emu_, cf));
        };
        items.push_back(std::move(it));
    }
    {
        SlotEntry it;
        it.id    = "generate_fat32";
        it.label = "Generate FAT32 into CF.IMG and choose it...";
        it.insert = [this, inserter] {
            const std::vector<std::string> files = ChooseFilesToGenerate();
            if (files.empty()) return;
            const uint32_t min_mb = std::max<uint32_t>(PayloadMb(files), 32u);
            const uint32_t size_mb = PromptSizeMb(min_mb);
            if (size_mb == 0) return;
            const std::string cf = CfImgPath();
            if (emu_.Get<CompactFlashFat32Builder>().Build(cf, files, size_mb))
                inserter(std::make_unique<CompactFlashCard>(emu_, cf));
        };
        items.push_back(std::move(it));
    }
    return entry;
}
