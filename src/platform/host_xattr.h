#pragma once

#include <cstddef>
#include <sys/types.h>

namespace cerfcore {

namespace host_platform {

bool    XattrAbsent(int err);
ssize_t GetXattr(const char* path, const char* name, void* value, size_t size);
ssize_t GetXattrFd(int fd, const char* name, void* value, size_t size);
int     SetXattr(const char* path, const char* name, const void* value, size_t size);
int     SetXattrFd(int fd, const char* name, const void* value, size_t size);
int     RemoveXattr(const char* path, const char* name);

}

}
