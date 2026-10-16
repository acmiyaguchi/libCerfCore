#pragma once

#include <cstdint>

/* OMAP3530 TRM SPRUF98Y §16.2.4.2.1 (printed p. 2608): "the timer input clock
   is 32,768 Hz". §16.6.1 (printed p. 2660): the sync counter is clocked by the
   32-kHz system clock. */
constexpr uint64_t kOmap3530Clk32kHz = 32768ull;
