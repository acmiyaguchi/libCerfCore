#pragma once

#include <cerfcore/peripherals/peripheral_base.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_addr_map.h>

#include <cstdint>
#include <mutex>
#include <string>

class CerfLogChannelPeripheral : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override;
    uint32_t MmioSize() const override;

    uint8_t  ReadByte (uint32_t addr) override;
    uint16_t ReadHalf (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

private:
    void Dispatch(uint32_t addr, char c);
    void Fatal(uint32_t id);
    void AppendChar(uint32_t id, char c);

    std::mutex  mutex_;
    std::string line_[CerfVirt::kLogChannelCount];
};
