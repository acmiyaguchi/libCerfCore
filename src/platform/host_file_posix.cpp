#include <cerfcore/platform/host_file.h>

#include "host_unimplemented.h"

namespace host_platform {

HostFile::~HostFile() {
    if (handle_) NoHostImplementation("HostFile::~HostFile");
}

bool HostFile::Open(const std::string&, FileMode) { NoHostImplementation("HostFile::Open"); }
void HostFile::Close() { NoHostImplementation("HostFile::Close"); }
bool HostFile::Size(uint64_t&) const { NoHostImplementation("HostFile::Size"); }
bool HostFile::SeekTo(uint64_t) { NoHostImplementation("HostFile::SeekTo"); }
bool HostFile::SeekToEnd() { NoHostImplementation("HostFile::SeekToEnd"); }
bool HostFile::Read(void*, uint32_t, uint32_t&) { NoHostImplementation("HostFile::Read"); }
bool HostFile::Write(const void*, uint32_t, uint32_t&) { NoHostImplementation("HostFile::Write"); }
void HostFile::MarkSparse() { NoHostImplementation("HostFile::MarkSparse"); }
bool HostFile::ZeroRange(uint64_t, uint64_t) { NoHostImplementation("HostFile::ZeroRange"); }

DeleteResult DeleteHostFile(const std::string&, uint32_t&) {
    NoHostImplementation("DeleteHostFile");
}

bool ReplaceHostFile(const std::string&, const std::string&, bool) {
    NoHostImplementation("ReplaceHostFile");
}

bool HostFileExists(const std::string&) { NoHostImplementation("HostFileExists"); }
bool HostPathExists(const std::string&) { NoHostImplementation("HostPathExists"); }

}
