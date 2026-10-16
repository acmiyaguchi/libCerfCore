#pragma once

#include <cstdint>

namespace CerfVirt {

constexpr uint32_t kFsServerPbAddr = 0x00;
constexpr uint32_t kFsCode         = 0x04;
constexpr uint32_t kFsIoPending    = 0x08;
constexpr uint32_t kFsResult       = 0x0C;
constexpr uint32_t kFsEnabled      = 0x10;
constexpr uint32_t kFsGeneration   = 0x14;
constexpr uint32_t kFsMountPoint   = 0x20;
constexpr uint32_t kFsMountPointMaxWchars = 64;

constexpr uint32_t kServerPollCompletion = 0x00;
constexpr uint32_t kServerGetDriveConfig = 0x04;
constexpr uint32_t kServerCreate         = 0x05;
constexpr uint32_t kServerOpen           = 0x06;
constexpr uint32_t kServerRead           = 0x07;
constexpr uint32_t kServerWrite          = 0x08;
constexpr uint32_t kServerSetEOF         = 0x09;
constexpr uint32_t kServerClose          = 0x0A;
constexpr uint32_t kServerGetSpace       = 0x0B;
constexpr uint32_t kServerMkDir          = 0x0C;
constexpr uint32_t kServerRmDir          = 0x0D;
constexpr uint32_t kServerSetAttributes  = 0x0E;
constexpr uint32_t kServerRename         = 0x0F;
constexpr uint32_t kServerDelete         = 0x10;
constexpr uint32_t kServerGetInfo        = 0x11;
constexpr uint32_t kServerGetFCBInfo     = 0x13;
constexpr uint32_t kServerGetMaxIOSize   = 0x15;
constexpr uint32_t kServerFindClose      = 0x16;

constexpr uint16_t kErrorNoError              = 0;
constexpr uint16_t kErrorFileNotFound         = 0x0002;
constexpr uint16_t kErrorPathNotFound         = 0x0003;
constexpr uint16_t kErrorTooManyOpenFiles     = 0x0004;
constexpr uint16_t kErrorAccessDenied         = 0x0005;
constexpr uint16_t kErrorInvalidHandle        = 0x0006;
constexpr uint16_t kErrorNoMoreFiles          = 0x0012;
constexpr uint16_t kErrorWriteProtect         = 0x0013;
constexpr uint16_t kErrorWriteFault           = 0x001D;
constexpr uint16_t kErrorReadFault            = 0x001E;
constexpr uint16_t kErrorGeneralFailure       = 0x001F;
constexpr uint16_t kErrorSharingViolation     = 0x0020;
constexpr uint16_t kErrorLockViolation        = 0x0021;
constexpr uint16_t kErrorDiskFull             = 0x0027;
constexpr uint16_t kErrorFileExists           = 0x0050;
constexpr uint16_t kErrorInvalidName          = 0x007B;
constexpr uint16_t kErrorAlreadyExists        = 0x00B7;

constexpr uint16_t kOpenAccessReadOnly   = 0x0000;
constexpr uint16_t kOpenAccessWriteOnly  = 0x0001;
constexpr uint16_t kOpenAccessReadWrite  = 0x0002;
constexpr uint16_t kOpenShareDenyReadWrite = 0x0010;
constexpr uint16_t kOpenShareDenyWrite     = 0x0020;
constexpr uint16_t kOpenShareDenyRead      = 0x0030;
constexpr uint16_t kOpenShareDenyNone      = 0x0040;

constexpr uint32_t kMaxLfn = 255;
constexpr uint32_t kFolderShareMaxReadWriteSize = 1024 * 64;

struct ServerPB {
    uint16_t fStructureSize;
    uint16_t fResult;
    uint32_t fFindTransactionID;
    int16_t  fIndex;
    uint16_t fHandle;
    uint32_t fFileTimeDate;
    uint32_t fSize;
    uint32_t fPosition;
    uint32_t fDTAPtr;
    uint16_t fFileAttributes;
    uint16_t fOpenMode;
    uint8_t  fWildCard;
    struct {
        uint16_t fNameLength;
        uint16_t fName[kMaxLfn + 1];
        uint16_t fName2Length;
        uint16_t fName2[kMaxLfn + 1];
    } fLfn;
    uint32_t fFileCreateTimeDate;
};

static_assert(sizeof(ServerPB) == 1068,
              "ServerPB layout must stay byte-identical to the guest FSD's");

}
