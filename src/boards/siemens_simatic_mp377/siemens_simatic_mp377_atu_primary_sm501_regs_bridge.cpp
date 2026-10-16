#include "siemens_simatic_mp377_board_io_window.h"

namespace cerfcore {

using namespace siemens_simatic_mp377_board_io_detail;

namespace {

class SiemensSimaticMp377AtuPrimarySm501RegsBridge : public SiemensSimaticMp377BoardIoWindow {
public:
    using SiemensSimaticMp377BoardIoWindow::SiemensSimaticMp377BoardIoWindow;

    uint32_t MmioBase() const override { return kAtuPrimarySm501RegsBase; }
    uint32_t MmioSize() const override { return siemens_simatic_mp377::kSm501RegsBytes; }

    uint8_t ReadByte(uint32_t addr) override {
        const uint32_t bus = AtuPrimarySm501RegsBusAddr(addr);
        const uint8_t value = emu_.Get<siemens_simatic_mp377::SiemensSimaticMp377Sm501Regs>().ReadByte(bus);
        return value;
    }
    uint16_t ReadHalf(uint32_t addr) override {
        const uint32_t bus = AtuPrimarySm501RegsBusAddr(addr);
        const uint16_t value = emu_.Get<siemens_simatic_mp377::SiemensSimaticMp377Sm501Regs>().ReadHalf(bus);
        return value;
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t bus = AtuPrimarySm501RegsBusAddr(addr);
        const uint32_t value = emu_.Get<siemens_simatic_mp377::SiemensSimaticMp377Sm501Regs>().ReadWord(bus);
        return value;
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        const uint32_t bus = AtuPrimarySm501RegsBusAddr(addr);
        emu_.Get<siemens_simatic_mp377::SiemensSimaticMp377Sm501Regs>().WriteByte(bus, value);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        const uint32_t bus = AtuPrimarySm501RegsBusAddr(addr);
        emu_.Get<siemens_simatic_mp377::SiemensSimaticMp377Sm501Regs>().WriteHalf(bus, value);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t bus = AtuPrimarySm501RegsBusAddr(addr);
        emu_.Get<siemens_simatic_mp377::SiemensSimaticMp377Sm501Regs>().WriteWord(bus, value);
    }

};

} // namespace

REGISTER_SERVICE(SiemensSimaticMp377AtuPrimarySm501RegsBridge);

}
