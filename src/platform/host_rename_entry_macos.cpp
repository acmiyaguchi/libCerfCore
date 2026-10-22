#include "host_rename_entry.h"

#include <cerrno>
#include <cstdio>
#include <fcntl.h>

namespace cerfcore {

namespace host_platform {

int RenameEntry(const char* from, const char* to, bool no_replace) {
    return renameatx_np(AT_FDCWD, from, AT_FDCWD, to, no_replace ? RENAME_EXCL : 0u);
}

bool RenameNoReplaceUnsupported(int err) { return err == ENOTSUP; }

}

}
