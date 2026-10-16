#pragma once

#include <cstdint>

/* Nino keypad KeyboardMap device_code: low 8 bits = pin, bit 8 set = a
   general-purpose I/O pin (else a multi-function I/O pin). Both pin spaces share
   the low bits, so the flag is what OnHostKey routes on. */
constexpr uint32_t kNinoKeypadIoPinFlag = 0x100u;
constexpr uint32_t kNinoKeypadPinMask   = 0x0FFu;
