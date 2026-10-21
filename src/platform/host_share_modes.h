#pragma once

#include "host_shared_folder.h"

#include <string>

namespace cerfcore {

namespace host_platform {

struct ShareClaim {
    bool read        = false;
    bool write       = false;
    bool share_read  = false;
    bool share_write = false;
};

SharedFsStatus ClaimShareMode(int lock_fd, const std::string& path, const ShareClaim& claim);
SharedFsStatus HoldSharedDirectory(const std::string& dir, int& fd);
SharedFsStatus CheckSharedEntryFree(const std::string& path);

}

}
