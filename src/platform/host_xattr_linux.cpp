#include "host_xattr.h"

#include <cerrno>
#include <sys/xattr.h>

namespace cerfcore {

namespace host_platform {

bool XattrAbsent(int err) { return err == ENODATA; }

ssize_t GetXattr(const char* path, const char* name, void* value, size_t size) {
    return getxattr(path, name, value, size);
}

ssize_t GetXattrFd(int fd, const char* name, void* value, size_t size) {
    return fgetxattr(fd, name, value, size);
}

int SetXattr(const char* path, const char* name, const void* value, size_t size) {
    return setxattr(path, name, value, size, 0);
}

int SetXattrFd(int fd, const char* name, const void* value, size_t size) {
    return fsetxattr(fd, name, value, size, 0);
}

int RemoveXattr(const char* path, const char* name) { return removexattr(path, name); }

}

}
