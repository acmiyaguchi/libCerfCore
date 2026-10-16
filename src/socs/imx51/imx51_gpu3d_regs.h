#pragma once

#include <cstdint>

/* i.MX51 z430 (AMD/Adreno A2xx) GPU3D register index map + PM4 opcodes.
   Each constant is grounded per-line against yamato_reg.h / kgsl_*.c /
   a2xx.xml; see the inline citation on each. */
namespace imx51_gpu3d_regs {

/* AMD Z430 GPU3D register block. MCIMX51RM Table 38-4: GC register space
   0x3000_0000..0x3002_FFFF, size 0x30000. */
constexpr uint32_t kBase = 0x30000000u;
constexpr uint32_t kSize = 0x00030000u;

/* RBBM_PM_OVERRIDE1/2 (0x39C/0x39D): clock-gating override config. R/W
   store+readback - the guest reads pm_override1 back (kgsl_drawctxt.c:479),
   so a write-only accept would corrupt its context save. */
constexpr uint32_t kIdxPmOverride1 = 0x039Cu;
constexpr uint32_t kIdxPmOverride2 = 0x039Du;

/* RBBM_SOFT_RESET (0x3C): write-only reset pulse (gsl_yamato_imx.c:342 writes
   0xFFFFFFFF then 0, never reads it); the stateless GPU3D model has no pipeline
   to reset. */
constexpr uint32_t kIdxSoftReset = 0x003Cu;

/* RBBM_CNTL (0x3B): init config, write-only (gsl_yamato_imx.c:347 writes it
   once, never reads it). */
constexpr uint32_t kIdxRbbmCntl = 0x003Bu;

/* MH_ARBITER_CONFIG (0xA40): memory-hub arbiter config, write-only on yamato
   (gsl_yamato_imx.c:350 writes it, never reads); CERF models no GPU memory
   arbitration. */
constexpr uint32_t kIdxMhArbiterConfig = 0x0A40u;

/* SQ_VS_PROGRAM/SQ_PS_PROGRAM (0x21F7/0x21F6): vertex/pixel shader program base,
   write-only (gsl_yamato_imx.c:353-354 clear both to 0 at init, never read);
   render-inert (the CP-ring scan FATALs before any shader-consuming draw). */
constexpr uint32_t kIdxSqVsProgram = 0x21F7u;
constexpr uint32_t kIdxSqPsProgram = 0x21F6u;

/* MH_MMU_CONFIG (0x40): GPU MMU config, write-only accept. The guest reports
   GSL_MMU_TRANSLATION_ENABLED FALSE (kgsl_hal_init) => gpuaddr==physical GPU-wide;
   modeling a translation here would mis-map the CP-ring and render-target
   physical addresses. */
constexpr uint32_t kIdxMhMmuConfig = 0x0040u;

/* MH_INTERRUPT_MASK (0xA42): MH error-interrupt enable (AXI error / MMU page
   fault). Write-only accept - CERF raises neither (faithful DRAM, MMU disabled),
   so the MH interrupt never fires. */
constexpr uint32_t kIdxMhInterruptMask = 0x0A42u;

constexpr uint32_t kIdxRbbmIntCntl = 0x03B4u;  /* RBBM_INT_CNTL, kgsl_yamato.c:34 */
constexpr uint32_t kIdxCpIntCntl   = 0x01F2u;  /* CP_INT_CNTL, kgsl_ringbuffer.c */
constexpr uint32_t kIdxRbWptrBase  = 0x01C8u;  /* RB_WPTR_BASE, kgsl_ringbuffer_start sub_C101D86C */
constexpr uint32_t kIdxRbWptrDelay = 0x01C6u;  /* RB_WPTR_DELAY */
constexpr uint32_t kIdxRbCntl      = 0x01C1u;  /* RB_CNTL, kgsl_ringbuffer_start sub_C101D86C (RMW) */

/* RBBM_STATUS (0x5D0): the idle poll. kgsl_yamato_idle waits for GUI_ACTIVE
   (bit31) clear (gsl_yamato_imx.c:769) / status ==0x110 (kgsl_yamato.c:792);
   CERF's GPU has no async pipeline, so it is always idle. */
constexpr uint32_t kIdxRbbmStatus     = 0x05D0u;
constexpr uint32_t kRbbmStatusIdle    = 0x00000110u;

/* MASTER_INT_SIGNAL (0x3B7): the yamato ISR (sub_C101AF40) reads it and decodes the
   MH/CP/RBBM/SQ sub-block interrupt bits. CERF's GPU3D asserts no interrupt line
   (poll-based completion, RM Table 3-2 GPU3D=IRQ102 idle only) -> 0 = nothing pending. */
constexpr uint32_t kIdxMasterIntSignal = 0x03B7u;

/* SQ_INST_STORE_MANAGMENT (0xD02, a2xx.xml:1237): shader-store partitioning; the draw-context
   save reads it before any write (kgsl_drawctxt.c:1277), so an unwritten read is the power-on
   default (a2xx.xml documents no reset; no shader instruction store modelled -> 0). */
constexpr uint32_t kIdxSqInstStoreManagment = 0x0D02u;

/* NXP linux-imx a1638da9, yamato/22/yamato_offset.h:450-465. */
constexpr uint32_t kIdxScratchReg0 = 0x0578u;
constexpr uint32_t kIdxScratchReg2 = 0x057Au;
constexpr uint32_t kIdxScratchReg7 = 0x057Fu;

/* MH_MMU_MPU_BASE/END (0x46/0x47): memory-protection-unit range, write-only.
   The guest DISABLES the MPU (base=0, end=0xFFFFF000 = all pages;
   kgsl_yamato.c:994-999); CERF models no GPU memory protection. */
constexpr uint32_t kIdxMhMmuMpuBase = 0x0046u;
constexpr uint32_t kIdxMhMmuMpuEnd  = 0x0047u;

/* Chip-id read triple PERIPHID1/PERIPHID2/PATCH_RELEASE (0x3F9/0x3FA/0x1):
   getchipid packs these into chip_id, which getproperty copies to devinfo
   non-branching (gsl_yamato_imx.c:556) - nothing gates on it, so 0 is faithful
   (the real PERIPHID values are off-disk). */
constexpr uint32_t kIdxPeriphId1    = 0x03F9u;
constexpr uint32_t kIdxPeriphId2    = 0x03FAu;
constexpr uint32_t kIdxPatchRelease = 0x0001u;

/* CP ring registers (yamato_reg.h; kgsl_ringbuffer_start kgsl_ringbuffer.c:341).
   RB_BASE/RB_RPTR_ADDR feed the CP_RB_WPTR ring-scan; the rest are CP/render
   config that stays inert ONLY because the ring-scan FATALs on any non-ME_INIT
   opcode (no render/timestamp/ucode path runs to consume them). */
constexpr uint32_t kIdxRbEdramInfo  = 0x0F02u;  /* RB_EDRAM_INFO, yamato_reg.h:350 */
constexpr uint32_t kIdxRbBase       = 0x01C0u;  /* REG_CP_RB_BASE, yamato_reg.h:283 */
constexpr uint32_t kIdxRbRptrAddr   = 0x01C3u;  /* REG_CP_RB_RPTR_ADDR, yamato_reg.h:286 */
constexpr uint32_t kIdxRbWptr       = 0x01C5u;  /* REG_CP_RB_WPTR, yamato_reg.h:288 */
constexpr uint32_t kIdxScratchAddr  = 0x01DDu;  /* REG_SCRATCH_ADDR, yamato_reg.h:354 */
constexpr uint32_t kIdxScratchUmsk  = 0x01DCu;  /* REG_SCRATCH_UMSK, yamato_reg.h:357 */
constexpr uint32_t kIdxCpIntAck     = 0x01F4u;  /* REG_CP_INT_ACK, yamato_reg.h:273 */
constexpr uint32_t kIdxCpDebug      = 0x01FCu;  /* REG_CP_DEBUG, yamato_reg.h:268 */
constexpr uint32_t kIdxMeCntl       = 0x01F6u;  /* REG_CP_ME_CNTL, yamato_reg.h:276 */
constexpr uint32_t kIdxMeRamWaddr   = 0x01F8u;  /* REG_CP_ME_RAM_WADDR, yamato_reg.h:278 */
constexpr uint32_t kIdxMeRamData    = 0x01FAu;  /* REG_CP_ME_RAM_DATA, yamato_reg.h:277 */
constexpr uint32_t kIdxPfpUcodeAddr = 0x00C0u;  /* REG_CP_PFP_UCODE_ADDR, yamato_reg.h:280 */
constexpr uint32_t kIdxPfpUcodeData = 0x00C1u;  /* REG_CP_PFP_UCODE_DATA, yamato_reg.h:281 */
constexpr uint32_t kIdxQueueThresh  = 0x01D5u;  /* REG_CP_QUEUE_THRESHOLDS, yamato_reg.h:282 */

/* PM4 packet decode (kgsl_pm4types.h): type=hdr>>30; TYPE3 opcode=(hdr>>8)&0xFF,
   cnt=((hdr>>16)&0x3FFF)+1; TYPE0 writes cnt regs from regindx=hdr&0x7FFF. */
constexpr uint32_t kPm4Type0 = 0u;
constexpr uint32_t kPm4Type2 = 2u;
constexpr uint32_t kPm4Type3 = 3u;
constexpr uint32_t kPm4OpMeInit              = 0x48u;  /* PM4_ME_INIT */
constexpr uint32_t kPm4OpNop                 = 0x10u;  /* PM4_NOP */
constexpr uint32_t kPm4OpIndirectBuffer      = 0x3Fu;  /* PM4_INDIRECT_BUFFER */
constexpr uint32_t kPm4OpIndirectBufferPfd   = 0x37u;  /* PM4_INDIRECT_BUFFER_PFD */
constexpr uint32_t kPm4OpWaitForIdle         = 0x26u;  /* PM4_WAIT_FOR_IDLE */
constexpr uint32_t kPm4OpEventWrite          = 0x46u;  /* PM4_EVENT_WRITE */
constexpr uint32_t kPm4OpInterrupt           = 0x40u;  /* PM4_INTERRUPT */
constexpr uint32_t kPm4OpSetConstant         = 0x2Du;  /* PM4_SET_CONSTANT */
constexpr uint32_t kPm4OpLoadConstantContext = 0x2Eu;  /* PM4_LOAD_CONSTANT_CONTEXT */
constexpr uint32_t kPm4OpRegToMem            = 0x3Eu;  /* PM4_REG_TO_MEM, kgsl_pm4types.h:68 */
constexpr uint32_t kPm4OpMemWrite            = 0x3Du;  /* NXP gsl_pm4types.h: PM4_MEM_WRITE. */
constexpr uint32_t kPm4OpImStore             = 0x2Cu;  /* PM4_IM_STORE, kgsl_pm4types.h:148 */
constexpr uint32_t kPm4OpRegRmw              = 0x21u;  /* PM4_REG_RMW, kgsl_pm4types.h:65 */
constexpr uint32_t kPm4OpWaitRegEq           = 0x52u;  /* PM4_WAIT_REG_EQ, kgsl_pm4types.h:53 */
constexpr uint32_t kPm4OpImLoadImmediate     = 0x2Bu;  /* PM4_IM_LOAD_IMMEDIATE, kgsl_pm4types.h:121 */
constexpr uint32_t kPm4OpImLoad              = 0x27u;  /* PM4_IM_LOAD, kgsl_pm4types.h:118 */
constexpr uint32_t kPm4OpDrawIndx            = 0x22u;  /* PM4_DRAW_INDX, kgsl_pm4types.h:96 */
constexpr uint32_t kPm4OpInvalidateState     = 0x3Bu;  /* PM4_INVALIDATE_STATE, kgsl_pm4types.h:127 */
constexpr uint32_t kPm4OpSetShaderBases      = 0x4Au;  /* PM4_SET_SHADER_BASES, kgsl_pm4types.h:131 */
constexpr uint32_t kEventCacheFlushTs        = 4u;     /* CACHE_FLUSH_TS, yamato_reg.h:27 */
constexpr uint32_t kEventCacheFlush          = 6u;     /* CACHE_FLUSH, yamato_reg.h:29 */
constexpr uint32_t kRegToMemShadowFlag       = 1u << 30;  /* build_reg_to_mem_range flag, kgsl_drawctxt.c:439 */

/* SET_CONSTANT bank bases: the type field ((tgt>>16)&7) selects a constant bank whose
   values the draw-context save reads back as registers (reg_to_mem). type 4 = register
   0x2000+offset (GSL_HAL_SUBBLOCK_OFFSET, gsl_yamato_imx.c:286 / kgsl_drawctxt.c:360). */
constexpr uint32_t kScBaseAlu   = 0x4000u;  /* type 0: REG_SQ_CONSTANT_0, yamato_reg.h:392 */
constexpr uint32_t kScBaseFetch = 0x4800u;  /* type 1: REG_SQ_FETCH_0, yamato_reg.h:393 */
constexpr uint32_t kScBaseBool  = 0x4900u;  /* type 2: REG_SQ_CF_BOOLEANS, yamato_reg.h:359 */
constexpr uint32_t kScBaseLoop  = 0x4908u;  /* type 3: REG_SQ_CF_LOOP, yamato_reg.h:360 */
constexpr uint32_t kScBaseReg   = 0x2000u;  /* type 4: register file */

}  // namespace imx51_gpu3d_regs
