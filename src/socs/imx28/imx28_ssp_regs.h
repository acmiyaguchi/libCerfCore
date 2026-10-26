#pragma once

#include <cstdint>

namespace cerfcore {

namespace imx28_ssp {

/* MCIMX28RM Rev. 2 section 17.10 memory map. */
constexpr uint32_t kCtrl0     = 0x000u;
constexpr uint32_t kCmd0      = 0x010u;
constexpr uint32_t kCmd1      = 0x020u;
constexpr uint32_t kXferSize  = 0x030u;
constexpr uint32_t kBlockSize = 0x040u;
constexpr uint32_t kCompref   = 0x050u;
constexpr uint32_t kCompmask  = 0x060u;
constexpr uint32_t kTiming    = 0x070u;
constexpr uint32_t kCtrl1     = 0x080u;
constexpr uint32_t kData      = 0x090u;
constexpr uint32_t kSdresp0   = 0x0A0u;
constexpr uint32_t kSdresp3   = 0x0D0u;
constexpr uint32_t kDdrCtrl   = 0x0E0u;
constexpr uint32_t kDllCtrl   = 0x0F0u;
constexpr uint32_t kStatus    = 0x100u;
constexpr uint32_t kDllSts    = 0x110u;
constexpr uint32_t kDebug     = 0x120u;
constexpr uint32_t kVersion   = 0x130u;
constexpr uint32_t kMapEnd    = 0x140u;

/* Section 17.10.1 CTRL0, reset C000_0000h, RSVD0 [15:0]. */
constexpr uint32_t kCtrl0Reset        = 0xC0000000u;
constexpr uint32_t kCtrl0Writable     = 0xFFFF0000u;
constexpr uint32_t kCtrl0Run          = 1u << 29;
constexpr uint32_t kCtrl0SdioIrqCheck = 1u << 28;
constexpr uint32_t kCtrl0LockCs       = 1u << 27;
constexpr uint32_t kCtrl0Read         = 1u << 25;
constexpr uint32_t kCtrl0DataXfer     = 1u << 24;
constexpr uint32_t kCtrl0BusWidthMask = 3u << 22;
constexpr uint32_t kCtrl0BusWidthRsvd = 3u << 22;
constexpr uint32_t kCtrl0WaitForCmd   = 1u << 20;
constexpr uint32_t kCtrl0LongResp     = 1u << 19;
constexpr uint32_t kCtrl0CheckResp    = 1u << 18;
constexpr uint32_t kCtrl0GetResp      = 1u << 17;
constexpr uint32_t kCtrl0Enable       = 1u << 16;

/* Section 17.10.2 CMD0: RSVD0 [31:27], RSVD1 [19:8], CMD [7:0] (uses 5:0). */
constexpr uint32_t kCmd0Writable      = 0x07F000FFu;
constexpr uint32_t kCmd0SoftTerminate = 1u << 26;
constexpr uint32_t kCmd0DblDataRate   = 1u << 25;
constexpr uint32_t kCmd0PrimBootOp    = 1u << 24;
constexpr uint32_t kCmd0BootAck       = 1u << 23;
constexpr uint32_t kCmd0Unmodelled    =
    kCmd0SoftTerminate | kCmd0DblDataRate | kCmd0PrimBootOp | kCmd0BootAck;
constexpr uint32_t kCmd0CmdField      = 0xFFu;
constexpr uint32_t kCmd0CmdIndex      = 0x3Fu;

/* Section 17.10.4 XFER_SIZE reset 0000_0001h. */
constexpr uint32_t kXferSizeReset = 0x00000001u;

/* Section 17.10.5 BLOCK_SIZE: RSVD0 [31:28], BLOCK_COUNT [27:4], BLOCK_SIZE [3:0]. */
constexpr uint32_t kBlockSizeWritable   = 0x0FFFFFFFu;
constexpr uint32_t kBlockCountShift     = 4u;
constexpr uint32_t kBlockCountMask      = 0x00FFFFFFu;
constexpr uint32_t kBlockSizeFieldMask  = 0xFu;

/* Section 17.10.8 TIMING: CLOCK_DIVIDE [15:8], an even value from 2 to 254. */
constexpr uint32_t kTimingDivideShift = 8u;
constexpr uint32_t kTimingDivideMask  = 0xFFu;

/* Section 17.10.9 CTRL1, reset 0000_0080h, RSVD2 [19], RSVD1 [18], RSVD0 [12]; each IRQ status
   bit has its enable one bit below it. */
constexpr uint32_t kCtrl1Reset          = 0x00000080u;
constexpr uint32_t kCtrl1Writable       = 0xFFF3EFFFu;
constexpr uint32_t kCtrl1IrqStatus      = 0xAAA28000u;
constexpr uint32_t kCtrl1RespTimeoutIrq = 1u << 27;
constexpr uint32_t kCtrl1DmaEnable      = 1u << 13;
constexpr uint32_t kCtrl1SlaveMode      = 1u << 8;
constexpr uint32_t kCtrl1WordLenShift   = 4u;
constexpr uint32_t kCtrl1WordLenMask    = 0xFu;
constexpr uint32_t kCtrl1WordLen8Bits   = 0x7u;
constexpr uint32_t kCtrl1WordLen16Bits  = 0xFu;
constexpr uint32_t kCtrl1ModeMask       = 0xFu;
constexpr uint32_t kCtrl1ModeSdMmc      = 0x3u;

/* Section 17.10.15 DDR_CTRL: RSVD0 [29:2]. */
constexpr uint32_t kDdrCtrlWritable = 0xC0000003u;

/* Section 17.10.16 DLL_CTRL register figure: RSVD1 [19:16], RSVD0 [8]. */
constexpr uint32_t kDllCtrlWritable = 0xFFF0FEFFu;

/* § 17.10.17 STATUS reset E000_0020h: PRESENT, RSVD4, SD_PRESENT, FIFO_EMPTY;
   RESP_TIMEOUT [14], FIFO_EMPTY [5], DATA_BUSY [2], BUSY [0]. */
constexpr uint32_t kStatusReset       = 0xE0000020u;
constexpr uint32_t kStatusRespTimeout = 1u << 14;
constexpr uint32_t kStatusFifoEmpty   = 1u << 5;
constexpr uint32_t kStatusDataBusy    = 1u << 2;
constexpr uint32_t kStatusBusy        = 1u << 0;

/* Section 17.10.20 VERSION reset 0400_0000h. */
constexpr uint32_t kVersionValue = 0x04000000u;

}  // namespace imx28_ssp

}
