#pragma once

#include <cerfcore/core/service.h>
#include "../../platform/host_shared_folder.h"
#include "cerf_virt_folder_share_regs.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace cerfcore {

class StateWriter;
class StateReader;

class FolderShareFiles : public Service {
public:
    using Service::Service;
    ~FolderShareFiles() override;

    bool ShouldRegister() override;
    void OnReady() override;

    static bool Owns(uint32_t code);

    uint32_t Run(uint32_t code, CerfVirt::ServerPB& pb);

    void CloseAll();
    void ReconcileGeneration();

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    struct OpenFile {
        std::unique_ptr<host_platform::SharedFsFile> file;
        std::u16string                               name;
    };
    std::unordered_map<uint16_t, OpenFile> files_;
    uint16_t next_handle_       = 0;
    uint32_t config_generation_ = 0;

    OpenFile* FileFor(uint16_t handle);
    bool      AllocHandle(uint16_t& handle);

    uint16_t GetDriveConfig(CerfVirt::ServerPB& pb);
    uint16_t Create(CerfVirt::ServerPB& pb);
    uint16_t Open(CerfVirt::ServerPB& pb);
    uint16_t Read(CerfVirt::ServerPB& pb);
    uint16_t Write(CerfVirt::ServerPB& pb);
    uint16_t SetEof(CerfVirt::ServerPB& pb);
    uint16_t Close(CerfVirt::ServerPB& pb);
    uint16_t GetFcbInfo(CerfVirt::ServerPB& pb);
    uint16_t GetSpace(CerfVirt::ServerPB& pb);
};

}
