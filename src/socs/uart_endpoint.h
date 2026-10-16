#pragma once

#include "uart_line_device.h"

#include <cstdint>

class UartEndpoint : public UartLineDevice {
public:
    virtual void OnGuestTx(uint8_t byte) = 0;
};
