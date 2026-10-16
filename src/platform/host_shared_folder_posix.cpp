#include "host_shared_folder.h"

#include "host_unimplemented.h"

namespace host_platform {

SharedPathResult ResolveSharedPath(const std::string&, std::u16string_view, std::u16string&) {
    NoHostImplementation("ResolveSharedPath");
}

SharedFsStatus CreateSharedDirectory(const std::u16string&) {
    NoHostImplementation("CreateSharedDirectory");
}
SharedFsStatus RemoveSharedDirectory(const std::u16string&) {
    NoHostImplementation("RemoveSharedDirectory");
}
SharedFsStatus DeleteSharedFile(const std::u16string&) {
    NoHostImplementation("DeleteSharedFile");
}
SharedFsStatus MoveSharedFile(const std::u16string&, const std::u16string&) {
    NoHostImplementation("MoveSharedFile");
}
SharedFsStatus SetSharedWriteTime(const std::u16string&, uint32_t) {
    NoHostImplementation("SetSharedWriteTime");
}
SharedFsStatus SetSharedAttributes(const std::u16string&, uint8_t) {
    NoHostImplementation("SetSharedAttributes");
}
SharedFsStatus QuerySharedInfo(const std::u16string&, SharedFsInfo&) {
    NoHostImplementation("QuerySharedInfo");
}
SharedFsStatus QuerySharedSpace(const std::string&, uint64_t&, uint64_t&) {
    NoHostImplementation("QuerySharedSpace");
}
SharedFsStatus CreateNewSharedFile(const std::u16string&) {
    NoHostImplementation("CreateNewSharedFile");
}

SharedFsFind::~SharedFsFind() {
    if (handle_ != nullptr) NoHostImplementation("SharedFsFind::~SharedFsFind");
}
SharedFsStatus SharedFsFind::First(const std::u16string&, SharedFsInfo&, std::u16string&) {
    NoHostImplementation("SharedFsFind::First");
}
SharedFsStatus SharedFsFind::Next(SharedFsInfo&, std::u16string&) {
    NoHostImplementation("SharedFsFind::Next");
}

SharedFsFile::~SharedFsFile() {
    if (handle_ != nullptr) NoHostImplementation("SharedFsFile::~SharedFsFile");
}
SharedFsStatus SharedFsFile::Open(const std::u16string&, SharedFileAccess, SharedFileShare) {
    NoHostImplementation("SharedFsFile::Open");
}
SharedFsStatus SharedFsFile::ReadAt(uint64_t, void*, uint32_t, uint32_t&) {
    NoHostImplementation("SharedFsFile::ReadAt");
}
SharedFsStatus SharedFsFile::WriteAt(uint64_t, const void*, uint32_t, uint32_t&) {
    NoHostImplementation("SharedFsFile::WriteAt");
}
SharedFsStatus SharedFsFile::TruncateAt(uint64_t) {
    NoHostImplementation("SharedFsFile::TruncateAt");
}
SharedFsStatus SharedFsFile::Info(SharedFsInfo&) {
    NoHostImplementation("SharedFsFile::Info");
}

}
