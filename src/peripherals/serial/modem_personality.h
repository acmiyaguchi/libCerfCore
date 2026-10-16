#pragma once

#include "serial_endpoint.h"

#include <cstdint>
#include <memory>
#include <string>

class CerfEmulator;
class PppTerminator;

/* Hayes AT-command modem behind the serial PC card's 16550. Answers the host
   TAPI stack (Unimodem): AT -> OK, dial -> CONNECT, tracking echo/verbose and
   the carrier (DCD) line. After CONNECT, online-mode bytes are a PPP stream
   handed to PppTerminator, which bridges the guest to the host network. */
class ModemPersonality : public SerialEndpoint {
public:
    ModemPersonality(CerfEmulator& emu, std::string net_id);
    ~ModemPersonality() override;

    void OnGuestTx(const uint8_t* data, size_t n) override;
    void OnControlLines(bool dtr, bool rts) override;
    void OnOpen() override;
    void OnClose() override;
    void ResendModemInputs() override;

private:
    void HandleCommandByte(uint8_t b);
    void ProcessCommandLine();
    void Reply(const char* verbose_text, char numeric_code);
    void PushStr(const char* s);
    void SetCarrier(bool on);

    CerfEmulator& emu_;
    std::string   net_id_;
    std::unique_ptr<PppTerminator> terminator_;

    std::string line_;
    bool echo_    = true;
    bool verbose_ = true;
    bool online_  = false;   /* data (connected) mode */
};
