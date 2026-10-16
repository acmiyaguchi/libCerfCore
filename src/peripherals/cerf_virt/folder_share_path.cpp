#include "folder_share_path.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/folder_share_config.h>
#include <cerfcore/core/log.h>

namespace cerfcore {

REGISTER_SERVICE(FolderSharePath);

using host_platform::SharedFsError;
using host_platform::SharedPathResult;

uint16_t FolderSharePath::ToHostPath(const uint16_t* ce_name,
                                     uint16_t ce_len_bytes,
                                     std::u16string& out) const {
    if (ce_len_bytes > CerfVirt::kMaxLfn * sizeof(uint16_t))
        ce_len_bytes = (uint16_t)(CerfVirt::kMaxLfn * sizeof(uint16_t));
    const std::u16string name(ce_name, ce_name + ce_len_bytes / sizeof(uint16_t));

    const SharedPathResult r = host_platform::ResolveSharedPath(
        emu_.Get<FolderShareConfig>().HostRoot(), name, out);
    if (r == SharedPathResult::kResolved) return CerfVirt::kErrorNoError;
    if (r == SharedPathResult::kRootUnavailable) return CerfVirt::kErrorPathNotFound;
    return CerfVirt::kErrorInvalidName;
}

void FolderSharePath::FillInfo(CerfVirt::ServerPB& pb, const host_platform::SharedFsInfo& info) {
    pb.fFileAttributes     = info.attributes;
    pb.fSize               = info.size > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)info.size;
    pb.fFileTimeDate       = info.write_time;
    pb.fFileCreateTimeDate = info.create_time;
}

void FolderSharePath::FillName(CerfVirt::ServerPB& pb, const std::u16string& name) {
    size_t n = name.size();
    if (n > CerfVirt::kMaxLfn) n = CerfVirt::kMaxLfn;
    for (size_t i = 0; i < n; ++i) pb.fLfn.fName[i] = name[i];
    pb.fLfn.fName[n]    = 0;
    pb.fLfn.fNameLength = (uint16_t)(n * sizeof(uint16_t));
}

uint16_t FolderSharePath::CeError(const host_platform::SharedFsStatus& status) {
    switch (status.error) {
        case SharedFsError::kNone:             return CerfVirt::kErrorNoError;
        case SharedFsError::kFileNotFound:     return CerfVirt::kErrorFileNotFound;
        case SharedFsError::kPathNotFound:     return CerfVirt::kErrorPathNotFound;
        case SharedFsError::kTooManyOpenFiles: return CerfVirt::kErrorTooManyOpenFiles;
        case SharedFsError::kAccessDenied:     return CerfVirt::kErrorAccessDenied;
        case SharedFsError::kDirNotEmpty:      return CerfVirt::kErrorAccessDenied;
        case SharedFsError::kInvalidHandle:    return CerfVirt::kErrorInvalidHandle;
        case SharedFsError::kNoMoreFiles:      return CerfVirt::kErrorNoMoreFiles;
        case SharedFsError::kWriteProtect:     return CerfVirt::kErrorWriteProtect;
        case SharedFsError::kWriteFault:       return CerfVirt::kErrorWriteFault;
        case SharedFsError::kReadFault:        return CerfVirt::kErrorReadFault;
        case SharedFsError::kSharingViolation: return CerfVirt::kErrorSharingViolation;
        case SharedFsError::kLockViolation:    return CerfVirt::kErrorLockViolation;
        case SharedFsError::kDiskFull:         return CerfVirt::kErrorDiskFull;
        case SharedFsError::kFileExists:       return CerfVirt::kErrorFileExists;
        case SharedFsError::kAlreadyExists:    return CerfVirt::kErrorAlreadyExists;
        case SharedFsError::kInvalidName:      return CerfVirt::kErrorInvalidName;
        case SharedFsError::kUnrepresentableTime: return CerfVirt::kErrorGeneralFailure;
        case SharedFsError::kUnmapped:         break;
    }
    LOG(Cerf, "[FolderShare] unmapped host error %u -> general failure\n", status.native);
    return CerfVirt::kErrorGeneralFailure;
}

}
