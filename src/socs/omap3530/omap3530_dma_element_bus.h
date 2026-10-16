#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

class EmulatedMemory;
class PeripheralDispatcher;

class Omap3530DmaElementBus : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t Read(uint32_t pa, uint32_t es, bool big_endian);
    void     Write(uint32_t pa, uint32_t es, bool big_endian, uint32_t value);

private:
    EmulatedMemory*       mem_  = nullptr;
    PeripheralDispatcher* disp_ = nullptr;
};
