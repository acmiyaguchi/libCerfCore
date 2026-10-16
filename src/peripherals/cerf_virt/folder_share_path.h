#pragma once

#include <cerfcore/core/service.h>
#include "../../platform/host_shared_folder.h"
#include "cerf_virt_folder_share_regs.h"

#include <cstdint>
#include <string>

namespace cerfcore {

class FolderSharePath : public Service {
public:
    using Service::Service;

    uint16_t ToHostPath(const uint16_t* ce_name, uint16_t ce_len_bytes,
                        std::u16string& out) const;

    static uint16_t CeError(const host_platform::SharedFsStatus& status);
    static void     FillInfo(CerfVirt::ServerPB& pb, const host_platform::SharedFsInfo& info);
    static void     FillName(CerfVirt::ServerPB& pb, const std::u16string& name);
};

}
