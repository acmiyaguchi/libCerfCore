#include "host_literal_entry.h"

#include "host_shared_entry.h"

#include <fcntl.h>
#include <linux/fs.h>
#include <linux/magic.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

namespace cerfcore {

namespace host_platform {

namespace {

bool DirectoryFoldsNames(const std::string& dir) {
    const int fd = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return false;
    int           flags = 0;
    struct statfs fs {};
    struct stat   marker {};
    const bool    folds = (ioctl(fd, FS_IOC_GETFLAGS, &flags) == 0 && (flags & FS_CASEFOLD_FL) != 0) ||
                       (fstatfs(fd, &fs) == 0 && fs.f_type == FUSE_SUPER_MAGIC &&
                        fstatat(fd, ".ciopfs", &marker, AT_NO_AUTOMOUNT) == 0);
    close(fd);
    return folds;
}

}

LiteralEntry LookUpLiteralEntry(const std::string& dir, const std::string& name, std::string& stored) {
    struct stat st {};
    if (lstat(JoinSharedPath(dir, name).c_str(), &st) != 0) return LiteralEntry::kAbsent;
    if (DirectoryFoldsNames(dir)) return LiteralEntry::kUnnamed;
    stored = name;
    return LiteralEntry::kNamed;
}

}

}
