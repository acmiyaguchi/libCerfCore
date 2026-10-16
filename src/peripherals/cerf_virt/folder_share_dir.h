#pragma once

#include <cerfcore/core/service.h>
#include "../../platform/host_shared_folder.h"
#include "cerf_virt_folder_share_regs.h"

#include <cstdint>
#include <memory>
#include <unordered_map>

class FolderShareDir : public Service {
public:
    using Service::Service;
    ~FolderShareDir() override;

    bool ShouldRegister() override;
    void OnReady() override;

    static bool Owns(uint32_t code);
    uint32_t Run(uint32_t code, CerfVirt::ServerPB& pb);

    void CloseAll();
    void ReconcileGeneration();

private:
    std::unordered_map<uint32_t, std::unique_ptr<host_platform::SharedFsFind>> finds_;
    uint32_t config_generation_ = 0;

    uint16_t MkDir(CerfVirt::ServerPB& pb);
    uint16_t RmDir(CerfVirt::ServerPB& pb);
    uint16_t Delete(CerfVirt::ServerPB& pb);
    uint16_t Rename(CerfVirt::ServerPB& pb);
    uint16_t SetAttributes(CerfVirt::ServerPB& pb);
    uint16_t GetInfo(CerfVirt::ServerPB& pb);
    uint16_t CloseFind(CerfVirt::ServerPB& pb);
};
