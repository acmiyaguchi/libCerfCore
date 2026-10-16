#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/host_fs_path.h>
#include <cerfcore/core/log.h>
#include <cerfcore/core/utf16.h>
#include <cerfcore/boards/board_context.h>
#include "ford_sync_gen2_family_id.h"
#include "../../socs/imx51/imx51_usboh3.h"
#include <cerfcore/host/file_chooser.h>
#include <cerfcore/host/host_widget_registry.h>
#include <cerfcore/host/user_alert.h>
#include <cerfcore/core/fatal.h>
#include "ford_sync_gen2_media_hub.h"
#include "ford_sync_gen2_media_hub_sd_reader.h"

#include <atomic>
#include <optional>

namespace cerfcore {

namespace {
class FordSyncGen2MediaSlots : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::FordSyncGen2;
    }

    class SlotWidget final : public HostWidget {
    public:
        SlotWidget(FordSyncGen2MediaSlots& owner, int slot) : owner_(owner), slot_(slot) {}
        std::string WidgetName() const override { return slot_ == FordSyncGen2MediaHub::kSdPort ? "Media Hub SD" : "Media Hub USB"; }
        WidgetGroup Group() const override { return WidgetGroup::Usb; }
        WidgetIcon Icon() const override {
            return ResourceIcon{slot_ == FordSyncGen2MediaHub::kSdPort ? "ICON_SD_MAP" : "ICON_USB"};
        }
        std::string Tooltip() const override { return WidgetName() + ": " + owner_.MediaName(slot_); }
        std::vector<WidgetControl> Controls() override { return owner_.Controls(slot_); }
        void RestoreWidgetState(StateReader&) override { ++owner_.generation_[slot_]; }
    private:
        FordSyncGen2MediaSlots& owner_;
        int slot_;
    };

    void OnReady() override {
        controller_ = &emu_.Get<Imx51Usboh3>();
        auto locked = controller_->LockHostPort();
        auto& root = controller_->OtgHostRootPort();
        root.SetRestoreFactory([](uint32_t kind) -> std::unique_ptr<UsbDevice> {
            return kind == FordSyncGen2MediaHub::kStateKind ? std::make_unique<FordSyncGen2MediaHub>() : nullptr;
        });
        root.SetRequiresDevice();
        root.Attach(std::make_unique<FordSyncGen2MediaHub>());
        for (int i = 0; i < FordSyncGen2MediaHub::kPortCount; ++i) {
            for (const auto& media : Entries(i)) {
                if (!media.insert_on_launch) continue;
                auto device = Open(i, media);
                if (!device) {
                    LOG(Caution, "Media Hub: cannot open launch image '%s'\n", media.file.c_str());
                    continue;
                }
                Hub().SetMedia(i, std::move(device));
            }
            widgets_[i] = std::make_unique<SlotWidget>(*this, i);
            emu_.Get<HostWidgetRegistry>().Register(widgets_[i].get());
        }
    }

private:
    const std::vector<BundledUsbMedia>& Entries(int slot) const {
        const auto& cfg = emu_.Get<DeviceConfig>();
        return slot == FordSyncGen2MediaHub::kSdPort ? cfg.bundled_sd_cards : cfg.bundled_usb_disks;
    }
    FordSyncGen2MediaHub& Hub() const {
        auto* device = controller_->OtgHostRootPort().Device();
        if (!device || device->StateKind() != FordSyncGen2MediaHub::kStateKind)
            emu_.Get<Fatal>().Die("Media Hub: the USB root port does not hold the Media Hub");
        return *static_cast<FordSyncGen2MediaHub*>(device);
    }
    UsbMassStorageDevice* Current(int slot) const {
        return static_cast<UsbMassStorageDevice*>(Hub().Port(slot).Device());
    }
    std::unique_ptr<UsbMassStorageDevice> Open(int slot, const BundledUsbMedia& media) {
        std::unique_ptr<UsbMassStorageDevice> device;
        if (slot == FordSyncGen2MediaHub::kSdPort) device = std::make_unique<FordSyncGen2MediaHubSdReader>(media.cid);
        else device = std::make_unique<UsbMassStorageDevice>();
        const auto& cfg = emu_.Get<DeviceConfig>();
        if (!device->OpenImage(ResolveDeviceFile(cfg.device_name, media.file), media.name)) return {};
        return device;
    }
    std::string MediaName(int slot) const {
        auto locked = controller_->LockHostPort();
        auto* device = Current(slot);
        if (!device) return "Empty";
        return device->ImageName();
    }
    void Apply(int slot, uint64_t generation, const BundledUsbMedia* media) {
        const std::string path = media
            ? ResolveDeviceFile(emu_.Get<DeviceConfig>().device_name, media->file) : std::string();
        {
            auto locked = controller_->LockHostPort();
            if (generation_[slot] != generation) return;
            auto* current = Current(slot);
            const bool same_file = media && current && current->ImagePath() == path;
            if (same_file && (slot == FordSyncGen2MediaHub::kUsbPort ||
                              static_cast<FordSyncGen2MediaHubSdReader*>(current)->Cid() == media->cid))
                return;
            if (!media || same_file) {
                Hub().SetMedia(slot, {});
                generation = ++generation_[slot];
            }
        }
        if (!media) return;
        auto device = Open(slot, *media);
        if (!device) {
            emu_.Get<UserAlert>().Show(AlertLevel::kError, AlertPlacement::kOverWindow, "Media Hub",
                                       "Cannot open existing media image: " + media->file);
            return;
        }
        auto locked = controller_->LockHostPort();
        if (generation_[slot] != generation) return;
        Hub().SetMedia(slot, std::move(device));
        ++generation_[slot];
    }
    void Browse(int slot, uint64_t generation) {
        const std::string file = emu_.Get<FileChooser>().ChooseFile(
            "Choose existing media image",
            {{"Disk images (*.img;*.bin;*.ima)", "*.img;*.bin;*.ima"}, {"All files", "*.*"}});
        if (file.empty()) return;
        BundledUsbMedia media;
        media.file = file;
        media.name = Utf16ToUtf8(HostFsPath(file).filename().u16string());
        Apply(slot, generation, &media);
    }
    std::optional<ChoiceOption> Inserted(int slot) const {
        auto locked = controller_->LockHostPort();
        auto* device = Current(slot);
        if (!device) return std::nullopt;
        return ChoiceOption{device->ImagePath(), device->ImageName()};
    }
    std::vector<WidgetControl> Controls(int slot) {
        const uint64_t generation = generation_[slot];
        SlotControl control;
        control.id   = "slot";
        control.kind = slot == FordSyncGen2MediaHub::kSdPort ? SlotKind::kSdCard : SlotKind::kUsbDisk;
        control.inserted = Inserted(slot);
        for (const auto& media : Entries(slot)) {
            SlotEntry entry;
            entry.id     = media.file;
            entry.label  = media.name;
            entry.insert = [this, slot, generation, media] { Apply(slot, generation, &media); };
            control.available.push_back(std::move(entry));
        }
        SlotEntry browse;
        browse.id     = "choose";
        browse.label  = "Choose image...";
        browse.insert = [this, slot, generation] { Browse(slot, generation); };
        control.available.push_back(std::move(browse));
        if (control.inserted)
            control.eject = [this, slot, generation] { Apply(slot, generation, nullptr); };
        return {WidgetControl{std::move(control)}};
    }
    Imx51Usboh3* controller_ = nullptr;
    std::unique_ptr<SlotWidget> widgets_[FordSyncGen2MediaHub::kPortCount];
    std::atomic<uint64_t> generation_[FordSyncGen2MediaHub::kPortCount]{};
};
}
REGISTER_SERVICE(FordSyncGen2MediaSlots);

}
