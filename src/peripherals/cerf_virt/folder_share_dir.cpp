#include "folder_share_dir.h"

#include "folder_share_path.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/folder_share_config.h>
#include <cerfcore/core/log.h>
#include <cerfcore/socs/guest_cpu_reset.h>

#include <string>

using namespace CerfVirt;
using host_platform::SharedFsError;
using host_platform::SharedFsInfo;
using host_platform::SharedFsStatus;

REGISTER_SERVICE(FolderShareDir);

namespace {

bool IsDotName(const std::u16string& n) {
    return n == u"." || n == u"..";
}

}

FolderShareDir::~FolderShareDir() {
    CloseAll();
}

bool FolderShareDir::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

void FolderShareDir::OnReady() {
    emu_.Get<GuestCpuReset>().RegisterResetListener(
        [this](ResetLineKind) { CloseAll(); });
}

void FolderShareDir::CloseAll() {
    finds_.clear();
}

void FolderShareDir::ReconcileGeneration() {
    const uint32_t g = emu_.Get<FolderShareConfig>().Generation();
    if (g != config_generation_) {
        config_generation_ = g;
        CloseAll();
    }
}

bool FolderShareDir::Owns(uint32_t code) {
    return code == kServerMkDir || code == kServerRmDir ||
           code == kServerSetAttributes || code == kServerRename ||
           code == kServerDelete || code == kServerGetInfo ||
           code == kServerFindClose;
}

uint32_t FolderShareDir::Run(uint32_t code, ServerPB& pb) {
    switch (code) {
        case kServerMkDir:         return MkDir(pb);
        case kServerRmDir:         return RmDir(pb);
        case kServerSetAttributes: return SetAttributes(pb);
        case kServerRename:        return Rename(pb);
        case kServerDelete:        return Delete(pb);
        case kServerGetInfo:       return GetInfo(pb);
        case kServerFindClose:     return CloseFind(pb);
        default: break;
    }
    LOG(Cerf, "[FolderShareDir] Run called with unowned op 0x%X\n", code);
    CerfFatalExit();
}

uint16_t FolderShareDir::MkDir(ServerPB& pb) {
    std::u16string path;
    uint16_t e = emu_.Get<FolderSharePath>().ToHostPath(
        pb.fLfn.fName, pb.fLfn.fNameLength, path);
    if (e != kErrorNoError) return e;
    const SharedFsStatus st = host_platform::CreateSharedDirectory(path);
    return st.Ok() ? kErrorNoError : FolderSharePath::CeError(st);
}

uint16_t FolderShareDir::RmDir(ServerPB& pb) {
    std::u16string path;
    uint16_t e = emu_.Get<FolderSharePath>().ToHostPath(
        pb.fLfn.fName, pb.fLfn.fNameLength, path);
    if (e != kErrorNoError) return e;
    const SharedFsStatus st = host_platform::RemoveSharedDirectory(path);
    return st.Ok() ? kErrorNoError : FolderSharePath::CeError(st);
}

uint16_t FolderShareDir::Delete(ServerPB& pb) {
    std::u16string path;
    uint16_t e = emu_.Get<FolderSharePath>().ToHostPath(
        pb.fLfn.fName, pb.fLfn.fNameLength, path);
    if (e != kErrorNoError) return e;
    const SharedFsStatus st = host_platform::DeleteSharedFile(path);
    return st.Ok() ? kErrorNoError : FolderSharePath::CeError(st);
}

uint16_t FolderShareDir::Rename(ServerPB& pb) {
    FolderSharePath& fsp = emu_.Get<FolderSharePath>();
    std::u16string src, dst;
    uint16_t e = fsp.ToHostPath(pb.fLfn.fName, pb.fLfn.fNameLength, src);
    if (e != kErrorNoError) return e;
    e = fsp.ToHostPath(pb.fLfn.fName2, pb.fLfn.fName2Length, dst);
    if (e != kErrorNoError) return e;
    const SharedFsStatus st = host_platform::MoveSharedFile(src, dst);
    return st.Ok() ? kErrorNoError : FolderSharePath::CeError(st);
}

uint16_t FolderShareDir::SetAttributes(ServerPB& pb) {
    std::u16string path;
    uint16_t e = emu_.Get<FolderSharePath>().ToHostPath(
        pb.fLfn.fName, pb.fLfn.fNameLength, path);
    if (e != kErrorNoError) return e;

    if (pb.fFileTimeDate != 0) {
        const SharedFsStatus st = host_platform::SetSharedWriteTime(path, pb.fFileTimeDate);
        if (!st.Ok()) return FolderSharePath::CeError(st);
    }

    const uint8_t attrs = (uint8_t)(pb.fFileAttributes &
                                    (host_platform::kSharedReadOnly | host_platform::kSharedHidden |
                                     host_platform::kSharedSystem | host_platform::kSharedArchive));
    const SharedFsStatus st = host_platform::SetSharedAttributes(path, attrs);
    return st.Ok() ? kErrorNoError : FolderSharePath::CeError(st);
}

uint16_t FolderShareDir::CloseFind(ServerPB& pb) {
    finds_.erase(pb.fFindTransactionID);
    return kErrorNoError;
}

uint16_t FolderShareDir::GetInfo(ServerPB& pb) {
    if (pb.fIndex == -1) {
        std::u16string path;
        uint16_t e = emu_.Get<FolderSharePath>().ToHostPath(
            pb.fLfn.fName, pb.fLfn.fNameLength, path);
        if (e != kErrorNoError) return e;
        SharedFsInfo info;
        const SharedFsStatus st = host_platform::QuerySharedInfo(path, info);
        if (!st.Ok()) return FolderSharePath::CeError(st);
        FolderSharePath::FillInfo(pb, info);
        return kErrorNoError;
    }

    const uint32_t tid = pb.fFindTransactionID;
    SharedFsInfo info;
    std::u16string name;
    if (pb.fIndex == 0) {
        finds_.erase(tid);
        std::u16string spec;
        uint16_t e = emu_.Get<FolderSharePath>().ToHostPath(
            pb.fLfn.fName, pb.fLfn.fNameLength, spec);
        if (e != kErrorNoError) return e;
        auto find = std::make_unique<host_platform::SharedFsFind>();
        const SharedFsStatus st = find->First(spec, info, name);
        if (!st.Ok()) {
            return (st.error == SharedFsError::kFileNotFound)
                       ? kErrorNoMoreFiles
                       : FolderSharePath::CeError(st);
        }
        while (IsDotName(name)) {
            if (!find->Next(info, name).Ok()) return kErrorNoMoreFiles;
        }
        finds_[tid] = std::move(find);
        FolderSharePath::FillInfo(pb, info);
        FolderSharePath::FillName(pb, name);
        return kErrorNoError;
    }

    auto it = finds_.find(tid);
    if (it == finds_.end()) return kErrorNoMoreFiles;
    do {
        if (!it->second->Next(info, name).Ok()) {
            finds_.erase(it);
            return kErrorNoMoreFiles;
        }
    } while (IsDotName(name));
    FolderSharePath::FillInfo(pb, info);
    FolderSharePath::FillName(pb, name);
    return kErrorNoError;
}
