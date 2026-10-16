#pragma once

#include <servicelocator/service.h>

class CerfEmulator;

class Service : public servicelocator::Service {
protected:
    CerfEmulator& emu_;

public:
    explicit Service(CerfEmulator& emu) : emu_(emu) {}
};
