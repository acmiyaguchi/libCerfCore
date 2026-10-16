#pragma once

#include <cerfcore/core/cerf_emulator.h>

#include <servicelocator/deferred_service.h>

template <typename T>
using DeferredService = servicelocator::DeferredService<CerfEmulator, T>;
