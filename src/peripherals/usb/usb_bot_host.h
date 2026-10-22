#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace cerfcore {

class CerfEmulator;
class StateReader;
class StateWriter;

/* USB Mass Storage Class Bulk-Only Transport 1.0 sections 5.1-5.2 host side: CBW, data, CSW. */
class UsbBotHost {
public:
    struct Result {
        uint32_t moved;
        bool     short_packet;
    };
    using Transact = std::function<bool(uint32_t token, uint8_t* data, uint32_t length, Result& out)>;

    explicit UsbBotHost(CerfEmulator& emu) : emu_(emu) {}

    void Start(uint32_t in_token, uint32_t out_token);
    bool Step(const Transact& port);
    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    enum Phase : uint32_t { kOff, kCbw, kData, kCsw, kDone };

    void SendCbw(const Transact& port);
    void ReadData(const Transact& port);
    void ReadCsw(const Transact& port);
    void Finish();
    [[noreturn]] void Die(const char* what, uint32_t a, uint32_t b) const;

    CerfEmulator&        emu_;
    uint32_t             phase_     = kOff;
    uint32_t             command_   = 0;
    uint32_t             retries_   = 0;
    uint32_t             in_token_  = 0;
    uint32_t             out_token_ = 0;
    uint32_t             tag_       = 0;
    uint32_t             length_    = 0;
    uint32_t             block_     = 0;
    std::vector<uint8_t> data_;
};

}
