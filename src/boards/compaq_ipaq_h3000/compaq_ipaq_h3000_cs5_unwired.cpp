#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "compaq_ipaq_h3000_family_id.h"
#include "../../tracing/kernel_debug_sink.h"
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdio>
#include <string>
#include <vector>

namespace cerfcore {

namespace {

/* Reads MUST return a value with top nibble = 0xB or kernel's probe
   check (status & 0xF0) == 0xB0 (sub_8005A228) fails, PPSH disables,
   and no debug bytes ship. */
constexpr uint8_t  kProbePassByte = 0xBFu;
constexpr uint32_t kProbePassWord = 0xBFBFBFBFu;

/* Data-byte register inside HTC's SuperIO chip area at CS5+0xFE0,
   identified by observing the kernel's PPSH banner ("\r\n*****")
   come out byte-by-byte at this PA. The +0xE60/0xE64 pair is the
   probe data/status register pair from sub_8005A150/sub_8005A228. */
constexpr uint32_t kDebugDataPa = 0x4A000FE0u;

class CompaqIpaqH3000Cs5Unwired : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::CompaqIpaqH3000;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x4A000000u; }
    uint32_t MmioSize() const override { return 0x01000000u; }

    uint8_t  ReadByte (uint32_t)              override { return kProbePassByte; }
    uint32_t ReadWord (uint32_t)              override { return kProbePassWord; }
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

private:
    std::vector<uint8_t> line_;

    void DebugByte(uint8_t b) {
        line_.push_back(b);
        if (b == '\n' || line_.size() >= 256) FlushLine();
    }
    void FlushLine() {
        std::string ascii;
        for (uint8_t c : line_) ascii.push_back((c >= 0x20 && c < 0x7F) ? char(c) : '.');
        emu_.Get<KernelDebugSink>().EmitLine(ascii, "PPSH");
        line_.clear();
    }
};

void CompaqIpaqH3000Cs5Unwired::WriteByte(uint32_t addr, uint8_t value) {
    if (addr == kDebugDataPa) DebugByte(value);
}

void CompaqIpaqH3000Cs5Unwired::WriteWord(uint32_t addr, uint32_t value) {
    if (addr == kDebugDataPa) DebugByte(static_cast<uint8_t>(value & 0xFFu));
}

}  /* namespace */

REGISTER_SERVICE(CompaqIpaqH3000Cs5Unwired);

}
