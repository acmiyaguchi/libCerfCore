#include "../../peripherals/intel_sa1111/sa1111_ssp_device.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/device_config.h>
#include "../../core/host_file_bytes.h"
#include <cerfcore/core/log.h>
#include <cerfcore/state/state_stream.h>
#include <cerfcore/boards/board_context.h>
#include "hp_jornada_700_family_id.h"

#include <cstdint>
#include <vector>

namespace {

class HpJornada700ConfigEeprom : public Sa1111SspDevice {
public:
    using Sa1111SspDevice::Sa1111SspDevice;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
    }

    void OnReady() override {
        const auto& cfg = emu_.Get<DeviceConfig>();
        if (cfg.rom_eeprom.empty()) return;
        const std::string path = ResolveDeviceFile(cfg.device_name, cfg.rom_eeprom);
        if (!TryReadHostFileBytes(path, eeprom_)) {
            LOG(Caution, "HpJornada700ConfigEeprom: rom.eeprom '%s' not found at %s - EEPROM reads "
                "float 0xFF (OAL uses default config)\n", cfg.rom_eeprom.c_str(), path.c_str());
            return;
        }
        LOG(Boot, "HpJornada700ConfigEeprom: loaded config EEPROM %s (%lld bytes)\n",
            cfg.rom_eeprom.c_str(), static_cast<long long>(eeprom_.size()));
    }

    uint32_t Shift(uint32_t out, uint32_t bits) override {
        uint32_t in = 0u;
        for (uint32_t i = bits; i-- > 0u;) {
            if (bit_ == 0u) out_byte_ = OutputByte();
            in       = (in << 1) | ((out_byte_ >> (7u - bit_)) & 1u);
            in_byte_ = static_cast<uint8_t>((in_byte_ << 1) | ((out >> i) & 1u));
            if (++bit_ == 8u) {
                InputByte(in_byte_);
                bit_     = 0u;
                in_byte_ = 0u;
            }
        }
        return in;
    }

    void EndTransaction() override {
        if (phase_ == Phase::WriteData) wel_ = false;
        phase_   = Phase::Cmd;
        bit_     = 0u;
        in_byte_ = 0u;
    }

    void SaveState(StateWriter& w) override {
        w.Write<uint64_t>("eeprom_count", eeprom_.size());
        if (!eeprom_.empty()) w.WriteBytes("eeprom", eeprom_.data(), eeprom_.size());
        w.Write("phase", phase_);
        w.Write("addr", addr_);
        w.Write("status_reg", status_reg_);
        w.Write("wel", wel_);
        w.Write("ee_bit", bit_);
        w.Write("ee_in_byte", in_byte_);
        w.Write("ee_out_byte", out_byte_);
    }

    void RestoreState(StateReader& r) override {
        uint64_t n = 0;
        r.Read("eeprom_count", n);
        eeprom_.resize(static_cast<size_t>(n));
        if (n) r.ReadBytes("eeprom", eeprom_.data(), static_cast<size_t>(n));
        r.Read("phase", phase_);
        r.Read("addr", addr_);
        r.Read("status_reg", status_reg_);
        r.Read("wel", wel_);
        r.Read("ee_bit", bit_);
        r.Read("ee_in_byte", in_byte_);
        r.Read("ee_out_byte", out_byte_);
    }

private:
    enum class Phase { Cmd, ReadAddr, ReadData, WriteAddr, WriteData, RdsrData, WrsrData, Drain };

    uint8_t EepromByte(uint32_t a) const { return a < eeprom_.size() ? eeprom_[a] : 0xFFu; }

    uint8_t OutputByte() const {
        if (phase_ == Phase::ReadData) return EepromByte(addr_ & 0xFFu);
        if (phase_ == Phase::RdsrData) return wel_ ? 0x02u : 0x00u;
        return 0xFFu;
    }

    /* jornada720 jornada720.bin hplib.dll sub_EA1C40: 1 WRSR, 2 WRITE, 3 READ, 4 WRDI, 5 RDSR,
       6 WREN. */
    void InputByte(uint8_t tx) {
        switch (phase_) {
            case Phase::Cmd:
                switch (tx) {
                    case 0x01: phase_ = Phase::WrsrData; break;
                    case 0x02: phase_ = Phase::WriteAddr; break;
                    case 0x03: phase_ = Phase::ReadAddr; break;
                    case 0x04: wel_ = false; break;
                    case 0x05: phase_ = Phase::RdsrData; break;
                    case 0x06: wel_ = true; break;
                    default:
                        LOG(Caution, "HpJornada700ConfigEeprom: unhandled EEPROM command 0x%02X - "
                            "responding 0xFF\n", tx);
                        phase_ = Phase::Drain;
                        break;
                }
                break;
            case Phase::ReadAddr:
                addr_  = tx;
                phase_ = Phase::ReadData;
                break;
            case Phase::ReadData:
                addr_++;
                break;
            case Phase::WriteAddr:
                addr_  = tx;
                phase_ = Phase::WriteData;
                break;
            case Phase::WriteData:
                if (wel_ && (addr_ & 0xFFu) < eeprom_.size()) {
                    eeprom_[addr_ & 0xFFu] = tx;
                    LOG(Periph, "[HpJornada700ConfigEeprom] EEPROM write [0x%02X]=0x%02X\n",
                        addr_ & 0xFFu, tx);
                }
                addr_++;
                break;
            case Phase::WrsrData:
                status_reg_ = tx;
                phase_      = Phase::Drain;
                break;
            case Phase::RdsrData:
            case Phase::Drain:
                break;
        }
    }

    std::vector<uint8_t> eeprom_;
    Phase                phase_      = Phase::Cmd;
    uint32_t             addr_       = 0;
    uint8_t              status_reg_ = 0;
    bool                 wel_        = false;
    uint32_t             bit_        = 0;
    uint8_t              in_byte_    = 0;
    uint8_t              out_byte_   = 0xFF;
};

}

REGISTER_SERVICE_AS(HpJornada700ConfigEeprom, Sa1111SspDevice);
