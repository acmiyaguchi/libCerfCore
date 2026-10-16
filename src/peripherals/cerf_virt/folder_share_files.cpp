#include "folder_share_files.h"

#include "folder_share_path.h"
#include "folder_share_stage.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/folder_share_config.h>
#include <cerfcore/core/log.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <string>

using namespace CerfVirt;
using host_platform::SharedFsInfo;
using host_platform::SharedFsStatus;

REGISTER_SERVICE(FolderShareFiles);

FolderShareFiles::~FolderShareFiles() {
    CloseAll();
}

bool FolderShareFiles::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

void FolderShareFiles::OnReady() {
    emu_.Get<GuestCpuReset>().RegisterResetListener(
        [this](ResetLineKind) { CloseAll(); });
}

void FolderShareFiles::CloseAll() {
    files_.clear();
}

void FolderShareFiles::ReconcileGeneration() {
    const uint32_t g = emu_.Get<FolderShareConfig>().Generation();
    if (g != config_generation_) {
        config_generation_ = g;
        CloseAll();
    }
}

void FolderShareFiles::SaveState(StateWriter& w) {
    w.Write("next_handle", next_handle_);
}

void FolderShareFiles::RestoreState(StateReader& r) {
    r.Read("next_handle", next_handle_);
    CloseAll();
}

FolderShareFiles::OpenFile* FolderShareFiles::FileFor(uint16_t handle) {
    auto it = files_.find(handle);
    return it == files_.end() ? nullptr : &it->second;
}

bool FolderShareFiles::Owns(uint32_t code) {
    return code == kServerGetDriveConfig || code == kServerCreate ||
           code == kServerOpen || code == kServerRead ||
           code == kServerWrite || code == kServerSetEOF ||
           code == kServerClose || code == kServerGetSpace ||
           code == kServerGetFCBInfo || code == kServerGetMaxIOSize;
}

uint32_t FolderShareFiles::Run(uint32_t code, ServerPB& pb) {
    switch (code) {
        case kServerGetDriveConfig: return GetDriveConfig(pb);
        case kServerCreate:         return Create(pb);
        case kServerOpen:           return Open(pb);
        case kServerRead:           return Read(pb);
        case kServerWrite:          return Write(pb);
        case kServerSetEOF:         return SetEof(pb);
        case kServerClose:          return Close(pb);
        case kServerGetSpace:       return GetSpace(pb);
        case kServerGetFCBInfo:     return GetFcbInfo(pb);
        case kServerGetMaxIOSize:
            LOG(Cerf, "[FolderShareFiles] GET_MAX_IO_SIZE has no issuing client\n");
            CerfFatalExit();
        default: break;
    }
    LOG(Cerf, "[FolderShareFiles] Run called with unowned op 0x%X\n", code);
    CerfFatalExit();
}

bool FolderShareFiles::AllocHandle(uint16_t& handle) {
    if (files_.size() > 0xFFFFu) return false;
    do {
        next_handle_ = (uint16_t)(next_handle_ + 1u);
    } while (files_.count(next_handle_) != 0);
    handle = next_handle_;
    return true;
}

uint16_t FolderShareFiles::GetDriveConfig(ServerPB&) {
    LOG(Cerf, "[FolderShareFiles] GET_DRIVE_CONFIG has no issuing client\n");
    CerfFatalExit();
}

uint16_t FolderShareFiles::Create(ServerPB& pb) {
    std::u16string path;
    uint16_t e = emu_.Get<FolderSharePath>().ToHostPath(
        pb.fLfn.fName, pb.fLfn.fNameLength, path);
    if (e != kErrorNoError) return e;
    const SharedFsStatus st = host_platform::CreateNewSharedFile(path);
    return st.Ok() ? kErrorNoError : FolderSharePath::CeError(st);
}

uint16_t FolderShareFiles::Open(ServerPB& pb) {
    std::u16string path;
    uint16_t e = emu_.Get<FolderSharePath>().ToHostPath(
        pb.fLfn.fName, pb.fLfn.fNameLength, path);
    if (e != kErrorNoError) return e;

    uint16_t handle;
    if (!AllocHandle(handle)) return kErrorTooManyOpenFiles;

    host_platform::SharedFileAccess access;
    switch (pb.fOpenMode & 0x03u) {
        case kOpenAccessReadOnly:  access = host_platform::SharedFileAccess::kRead; break;
        case kOpenAccessWriteOnly: access = host_platform::SharedFileAccess::kWrite; break;
        case kOpenAccessReadWrite: access = host_platform::SharedFileAccess::kReadWrite; break;
        default:
            LOG(Cerf, "[FolderShareFiles] Open with undefined access mode 0x%X\n",
                pb.fOpenMode);
            CerfFatalExit();
    }
    host_platform::SharedFileShare share;
    switch (pb.fOpenMode & 0x70u) {
        case kOpenShareDenyReadWrite: share = host_platform::SharedFileShare::kDenyReadWrite; break;
        case kOpenShareDenyWrite:     share = host_platform::SharedFileShare::kDenyWrite; break;
        case kOpenShareDenyRead:      share = host_platform::SharedFileShare::kDenyRead; break;
        case kOpenShareDenyNone:      share = host_platform::SharedFileShare::kDenyNone; break;
        default:
            LOG(Cerf, "[FolderShareFiles] Open with undefined share mode 0x%X\n",
                pb.fOpenMode);
            CerfFatalExit();
    }

    auto opened = std::make_unique<host_platform::SharedFsFile>();
    const SharedFsStatus st = opened->Open(path, access, share);
    if (!st.Ok()) return FolderSharePath::CeError(st);

    OpenFile& file = files_[handle];
    file.file = std::move(opened);
    file.name.assign(pb.fLfn.fName, pb.fLfn.fName + pb.fLfn.fNameLength / sizeof(uint16_t));
    pb.fHandle = handle;
    return kErrorNoError;
}

uint16_t FolderShareFiles::Read(ServerPB& pb) {
    OpenFile* file = FileFor(pb.fHandle);
    if (!file) return kErrorInvalidHandle;
    if (pb.fSize > kFolderShareMaxReadWriteSize) {
        LOG(Cerf, "[FolderShareFiles] Read of %u bytes exceeds the stage\n",
            pb.fSize);
        CerfFatalExit();
    }
    uint32_t done = 0;
    const SharedFsStatus st = file->file->ReadAt(
        pb.fPosition, emu_.Get<FolderShareStage>().IoBuf(), pb.fSize, done);
    if (!st.Ok()) return FolderSharePath::CeError(st);
    pb.fSize = done;
    return kErrorNoError;
}

uint16_t FolderShareFiles::Write(ServerPB& pb) {
    OpenFile* file = FileFor(pb.fHandle);
    if (!file) return kErrorInvalidHandle;
    if (pb.fSize > kFolderShareMaxReadWriteSize) {
        LOG(Cerf, "[FolderShareFiles] Write of %u bytes exceeds the stage\n",
            pb.fSize);
        CerfFatalExit();
    }
    uint32_t done = 0;
    const SharedFsStatus st = file->file->WriteAt(
        pb.fPosition, emu_.Get<FolderShareStage>().IoBuf(), pb.fSize, done);
    if (!st.Ok()) return FolderSharePath::CeError(st);
    pb.fSize = done;
    return kErrorNoError;
}

uint16_t FolderShareFiles::SetEof(ServerPB& pb) {
    OpenFile* file = FileFor(pb.fHandle);
    if (!file) return kErrorInvalidHandle;
    const SharedFsStatus st = file->file->TruncateAt(pb.fPosition);
    return st.Ok() ? kErrorNoError : FolderSharePath::CeError(st);
}

uint16_t FolderShareFiles::Close(ServerPB& pb) {
    auto it = files_.find(pb.fHandle);
    if (it == files_.end()) return kErrorInvalidHandle;
    files_.erase(it);
    return kErrorNoError;
}

uint16_t FolderShareFiles::GetFcbInfo(ServerPB& pb) {
    OpenFile* file = FileFor(pb.fHandle);
    if (!file) return kErrorInvalidHandle;
    SharedFsInfo info;
    const SharedFsStatus st = file->file->Info(info);
    if (!st.Ok()) return FolderSharePath::CeError(st);
    FolderSharePath::FillInfo(pb, info);
    FolderSharePath::FillName(pb, file->name);
    return kErrorNoError;
}

uint16_t FolderShareFiles::GetSpace(ServerPB& pb) {
    uint64_t total = 0;
    uint64_t avail = 0;
    const SharedFsStatus st = host_platform::QuerySharedSpace(
        emu_.Get<FolderShareConfig>().HostRoot(), total, avail);
    if (!st.Ok()) return FolderSharePath::CeError(st);
    const uint64_t cluster = 512ull * 64ull;
    uint64_t total_cl = total / cluster;
    uint64_t avail_cl = avail / cluster;
    if (total_cl > 0xFFFFFFFFull) total_cl = 0xFFFFFFFFull;
    if (avail_cl > 0xFFFFFFFFull) avail_cl = 0xFFFFFFFFull;
    pb.fPosition = (uint32_t)total_cl;
    pb.fSize     = (uint32_t)avail_cl;
    return kErrorNoError;
}
