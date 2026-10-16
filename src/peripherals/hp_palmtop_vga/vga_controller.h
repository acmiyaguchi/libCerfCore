#pragma once

#include <cerfcore/host/frame_source.h>

#include <cstdint>
#include <vector>

/* Standard VGA-compatible SVGA controller as found on the HP F1252A "HP
   Palmtop VGA" PC Card (and VGA-register-compatible siblings). The card's
   driver (jornada720 hpvgaout.dll / pcvgaout.dll, reverse-engineered) drives a
   textbook VGA register file memory-mapped in PC-Card common memory: it
   programs the Sequencer (0x3C4/5), CRTC (0x3D4/5), Graphics Controller
   (0x3CE/F), Attribute Controller (0x3C0), Misc Output (0x3C2) and RAMDAC
   (0x3C6/7/8/9), then selects pixel depth through the RAMDAC's hidden command
   register (0x3C6 after a 4-read unlock). Geometry is derived from the CRTC
   exactly as a real VGA does (refs: QEMU references/qemu_vga/vga.c
   vga_get_resolution / vga_get_params; register indices vga_regs.h).

   This is a plain object owned by the PCMCIA VGA card, which routes its
   common-memory register window (card offset 0) to ReadReg8/WriteReg8 and its
   framebuffer window (card offset 0x200000) to the framebuffer. It implements
   FrameSource so the card's external-monitor window renders it. */
class StateWriter;
class StateReader;

class VgaController : public FrameSource {
public:
    VgaController();

    /* Memory-mapped VGA register aperture (card common-memory offset 0). The
       byte sub-offset IS the classic VGA port (0x3C0..0x3DF, plus the card's
       0x43C8/0x43C9 pixel-clock extension). */
    uint8_t ReadReg8 (uint32_t port);
    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);
    void    WriteReg8(uint32_t port, uint8_t value);

    /* Linear framebuffer (card common-memory offset 0x200000). 8bpp covers up
       to 1024x768 (= 0xC0000); 16bpp is used only at 640x480 - both fit the
       card's 0xC0000 (768 KB) buffer (sub_100045DC maps that size). */
    static constexpr uint32_t kFbSize = 0x000C0000u;
    uint8_t* Framebuffer()            { return fb_.data(); }
    uint32_t FramebufferSize() const  { return kFbSize; }

    /* Current native display mode; false when no valid mode is programmed.
       The owning card polls this to size/title its window. */
    bool CurrentMode(uint32_t& w, uint32_t& h, uint32_t& bpp) const;

    /* FrameSource. */
    bool HasFrame() override;
    void RenderInto(uint32_t* dib_bgra32, uint32_t width, uint32_t height) override;

private:
    /* Geometry from the CRTC (VGA-standard; refs above). */
    uint32_t Width()       const;   /* (CR[H_DISP]+1) * 8                 */
    uint32_t Height()      const;   /* CR[V_DISP_END] | overflow bits, +1 */
    uint32_t StrideBytes() const;   /* CR[OFFSET] << 3                    */
    uint32_t StartByte()   const;   /* (CR[START_LO] | CR[START_HI]<<8)*4 */
    uint32_t Bpp()         const;   /* RAMDAC command reg: 8 or 16        */

    /* Indexed register groups (index registers select the entry; data
       registers read/write it). Sized 256 so any index byte is storable. */
    uint8_t cr_[256] = {};          /* CRTC          (idx 0x3D4 / data 0x3D5) */
    uint8_t sr_[256] = {};          /* Sequencer     (idx 0x3C4 / data 0x3C5) */
    uint8_t gr_[256] = {};          /* Graphics Ctrl (idx 0x3CE / data 0x3CF) */
    uint8_t ar_[32]  = {};          /* Attribute Ctrl(idx+data both 0x3C0)    */
    uint8_t misc_    = 0;           /* Misc Output (0x3C2)                    */

    uint8_t crtc_index_ = 0;
    uint8_t seq_index_  = 0;
    uint8_t gc_index_   = 0;
    uint8_t attr_index_ = 0;
    bool    attr_flipflop_ = false; /* 0x3C0 toggles index/data; 0x3DA resets */

    /* RAMDAC: 256-entry palette of 6-bit components, plus the hidden command
       register reached by reading the PEL-mask (0x3C6) four times. The driver
       writes command=0x00 for 8bpp palettized, 0x32 for 16bpp 5-6-5 hicolor
       (sub_10004CA4: read 0x3C6 x4 then write v16, v16=50 iff GMode 3). */
    uint8_t dac_pal_[256][3] = {};
    uint8_t dac_write_idx_   = 0;
    uint8_t dac_read_idx_    = 0;
    uint8_t dac_comp_        = 0;   /* 0=R 1=G 2=B, auto-increments */
    uint8_t dac_rgb_latch_[2] = {}; /* R,G held until the committing B write */
    uint8_t dac_command_     = 0;
    uint8_t pelmask_reads_   = 0;   /* consecutive 0x3C6 reads (4 = unlock)   */

    std::vector<uint8_t> fb_;
};
