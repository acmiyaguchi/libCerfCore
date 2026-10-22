#pragma once

#include <cstdint>

namespace cerfcore {

/* MCIMX28RM Rev. 2 section 31.3 memory map, offsets from USBCTRL0 0x8008_0000 and USBCTRL1
   0x8009_0000. */
enum Imx28UsbctrlOffset : uint32_t {
    kUsbId = 0x000, kUsbHwGeneral = 0x004, kUsbHwHost = 0x008, kUsbHwDevice = 0x00C,
    kUsbHwTxBuf = 0x010, kUsbHwRxBuf = 0x014,
    kUsbGptLd0 = 0x080, kUsbGptCtrl0 = 0x084, kUsbGptLd1 = 0x088, kUsbGptCtrl1 = 0x08C,
    kUsbSbusCfg = 0x090,
    kUsbCapLength = 0x100, kUsbHcsParams = 0x104, kUsbHccParams = 0x108,
    kUsbDciVersion = 0x120, kUsbDccParams = 0x124,
    kUsbCmd = 0x140, kUsbSts = 0x144, kUsbIntr = 0x148, kUsbFrIndex = 0x14C,
    kUsbAddrBase = 0x154, kUsbListAddr = 0x158, kUsbTtCtrl = 0x15C, kUsbBurstSize = 0x160,
    kUsbTxFillTuning = 0x164, kUsbIcUsb = 0x16C, kUsbUlpi = 0x170, kUsbEndptNak = 0x178,
    kUsbEndptNakEn = 0x17C, kUsbPortsc1 = 0x184, kUsbOtgsc = 0x1A4, kUsbMode = 0x1A8,
    kUsbEndptSetupStat = 0x1AC, kUsbEndptPrime = 0x1B0, kUsbEndptFlush = 0x1B4,
    kUsbEndptStat = 0x1B8, kUsbEndptComplete = 0x1BC, kUsbEndptCtrl0 = 0x1C0,
    kUsbMapEnd = 0x1E0
};

/* Section 31.3 memory map reset values of the read-only registers. */
struct Imx28UsbConst {
    uint32_t offset;
    uint32_t value;
};

constexpr Imx28UsbConst kImx28UsbConsts[] = {
    {kUsbId, 0xE401FA05u},        {kUsbHwGeneral, 0x00000015u}, {kUsbHwHost, 0x10020001u},
    {kUsbHwDevice, 0x00000011u},  {kUsbHwTxBuf, 0x80070A10u},   {kUsbHwRxBuf, 0x00000810u},
    {kUsbCapLength, 0x01000040u}, {kUsbHcsParams, 0x00010011u}, {kUsbHccParams, 0x00000006u},
    {kUsbDccParams, 0x00000188u}, {kUsbEndptStat, 0x00000000u}, {kUsbUlpi, 0x00000000u},
};

/* Section 31.3.17 USBCMD: ITC [23:16], FS2 15, ATDTW 14, SUTW 13, ASPE 11, ASP [9:8], IAA 6,
   ASE 5, PSE 4, FS1 3, FS0 2, RST 1, RS 0. */
constexpr uint32_t kUsbCmdReset     = 0x00080000u;
constexpr uint32_t kUsbCmdWritable  = 0x00FFEB7Fu;
constexpr uint32_t kUsbCmdRs        = 1u << 0;
constexpr uint32_t kUsbCmdRst       = 1u << 1;
constexpr uint32_t kUsbCmdIaa       = 1u << 6;
constexpr uint32_t kUsbCmdHostAspe  = 1u << 11;
constexpr uint32_t kUsbCmdHostAsp   = 3u << 8;

/* Section 31.3.18 USBSTS: write-1-to-clear TI1 25, TI0 24, UPI 19, UAI 18, SLI 8, SRI 7, URI 6,
   AAI 5, SEI 4, FRI 3, PCI 2, UEI 1, UI 0; NAKI [16] and HCH [12] are read-only. */
constexpr uint32_t kUsbStsW1c  = 0x030C01FFu;
constexpr uint32_t kUsbStsNaki = 1u << 16;
constexpr uint32_t kUsbStsHch  = 1u << 12;

/* Section 31.3.19 USBINTR enables sit at the USBSTS bit positions. */
constexpr uint32_t kUsbIntrWritable = 0x030D05FFu;

/* Section 31.3.34 USBMODE: SRT [15], VBPS [5], SDIS [4], SLOM [3], ES [2], CM [1:0]. */
constexpr uint32_t kUsbModeWritable = 0x0000803Fu;
constexpr uint32_t kUsbModeCmMask   = 0x3u;
constexpr uint32_t kUsbModeDevice   = 2u;
constexpr uint32_t kUsbModeHost     = 3u;

/* Section 31.3.32 PORTSC1: PTW [28], PFSC 24, PHCD 23, WKOC 22, WKDS 21, WKCN 20, PTC [19:16],
   PIC [15:14], PO 13, PP 12, LS [11:10], PE 2. */
constexpr uint32_t kUsbPortReset     = 0x10000000u;
constexpr uint32_t kUsbPortWritable  = 0x01FFF000u & ~((1u << 13) | (1u << 12));
constexpr uint32_t kUsbPortHostPp    = 1u << 12;
constexpr uint32_t kUsbPortLsUndef   = 3u << 10;
constexpr uint32_t kUsbPortPe        = 1u << 2;
constexpr uint32_t kUsbPortPtc       = 0xFu << 16;
constexpr uint32_t kUsbPortWakeHost  = (1u << 21) | (1u << 20);

/* Section 31.3.27 TXFILLTUNING: TXFIFOTHRES [21:16], TXSCHEALTH [12:8], TXSCHOH [6:0]. */
constexpr uint32_t kUsbTxFillMask    = 0x003F1F7Fu;

/* Section 31.3.33 OTGSC: enables [30:24], write-1-to-clear statuses [22:16], controls [7:0];
   reset 0x120 (ID 1, IDPU 1). */
constexpr uint32_t kUsbOtgscReset    = 0x00000120u;
constexpr uint32_t kUsbOtgscWritable = 0x7F0000FFu;
constexpr uint32_t kUsbOtgscW1c      = 0x007F0000u;
constexpr uint32_t kUsbOtgscEnables  = 0x7F000000u;
constexpr uint32_t kUsbOtgscOnemse   = 1u << 29;
constexpr uint32_t kUsbOtgscBseIs    = 1u << 20;
constexpr uint32_t kUsbOtgscBsvIs    = 1u << 19;
constexpr uint32_t kUsbOtgscAsvIs    = 1u << 18;
constexpr uint32_t kUsbOtgscAvvIs    = 1u << 17;
constexpr uint32_t kUsbOtgscOnemsIs  = 1u << 21;
constexpr uint32_t kUsbOtgscOnemst   = 1u << 13;
constexpr uint32_t kUsbOtgscBse      = 1u << 12;
constexpr uint32_t kUsbOtgscBsv      = 1u << 11;
constexpr uint32_t kUsbOtgscAsv      = 1u << 10;
constexpr uint32_t kUsbOtgscAvv      = 1u << 9;

/* Section 31.3.40-31.3.47 ENDPTCTRLn: EP0 reset 0x00800080 with TXS [16] and RXS [0] writable;
   EP1-7 reset 0 with TXE 23, TXI 21, TXT [19:18], TXD 17, TXS 16 and the RX fields at [7:0];
   TXR 22 and RXR 6 write a PID reset. */
constexpr uint32_t kUsbEp0Reset    = 0x00800080u;
constexpr uint32_t kUsbEp0Writable = 0x00010001u;
constexpr uint32_t kUsbEpWritable  = 0x00AF00AFu;
constexpr uint32_t kEndpoints      = 8u;

}
