#include "modem_personality.h"

#include "serial_line.h"
#include "ppp_terminator.h"

#include <cerfcore/core/log.h>

#include <cctype>
#include <cstdio>
#include <cstring>

namespace {
constexpr uint8_t kCR = 0x0D;
constexpr uint8_t kLF = 0x0A;
constexpr uint8_t kBS = 0x08;
}  /* namespace */

ModemPersonality::ModemPersonality(CerfEmulator& emu, std::string net_id)
    : emu_(emu), net_id_(std::move(net_id)) {}
ModemPersonality::~ModemPersonality() = default;

void ModemPersonality::OnOpen() {
    /* Live modem: assert DSR + CTS (ready + clear-to-send), carrier down. */
    if (uart_) uart_->SetModemInputs(/*cts=*/true, /*dsr=*/true, /*ri=*/false,
                                     /*dcd=*/false);
    line_.clear();
    echo_ = true;
    verbose_ = true;
    online_ = false;
}

void ModemPersonality::ResendModemInputs() {
    if (uart_) uart_->SetModemInputs(/*cts=*/true, /*dsr=*/true, /*ri=*/false,
                                     /*dcd=*/online_);
}

void ModemPersonality::OnClose() {
    if (uart_) uart_->SetModemInputs(false, false, false, false);
    online_ = false;
    if (terminator_) terminator_->Stop();
}

void ModemPersonality::OnControlLines(bool dtr, bool rts) {
    /* DTR 1->0 drops an online call (AT&D2). */
    if (!dtr && online_) SetCarrier(false);
    if (terminator_) terminator_->SetRts(rts);   /* RX flow control */
}

void ModemPersonality::OnGuestTx(const uint8_t* data, size_t n) {
    if (online_) {
        /* Data mode is a PPP stream; hand it to the terminator. PPP hangup is
           DTR-drop / LCP Terminate-Request, not the Hayes +++ escape. */
        if (terminator_) terminator_->OnGuestData(data, n);
        return;
    }
    for (size_t i = 0; i < n; ++i) HandleCommandByte(data[i]);
}

void ModemPersonality::HandleCommandByte(uint8_t b) {
    if (echo_ && uart_) uart_->PushRx(&b, 1);   /* command-mode local echo */
    if (b == kCR) { ProcessCommandLine(); line_.clear(); return; }
    if (b == kBS) { if (!line_.empty()) line_.pop_back(); return; }
    if (b == kLF) return;
    if (line_.size() < 256) line_.push_back((char)b);
}

void ModemPersonality::ProcessCommandLine() {
    std::string u;
    u.reserve(line_.size());
    for (char c : line_) u.push_back((char)toupper((unsigned char)c));

    LOG(Periph, "[Modem] cmd \"%s\"\n", u.c_str());

    if (u.empty()) return;                          /* bare CR: silent */
    if (u.rfind("AT", 0) != 0) { Reply("ERROR", '4'); return; }

    bool dial = false, hangup = false;
    for (size_t i = 2; i < u.size(); ++i) {
        const char c = u[i];
        const bool has_next = i + 1 < u.size();
        const char next = has_next ? u[i + 1] : '\0';
        switch (c) {
            case 'E': echo_    = (next == '1'); if (next=='0'||next=='1') ++i; break;
            case 'V': verbose_ = (next != '0'); if (next=='0'||next=='1') ++i; break;
            case 'D': dial = true; i = u.size(); break;   /* rest = dial string */
            case 'H': hangup = true; if (next=='0'||next=='1') ++i; break;
            case 'Z': echo_ = true; verbose_ = true; break;
            case '&':
                /* &-prefixed command (&C, &D, &F, …) plus optional digit. */
                if (has_next) {
                    if (next == 'F') { echo_ = true; verbose_ = true; }
                    ++i;
                    if (i + 1 < u.size() && isdigit((unsigned char)u[i + 1])) ++i;
                }
                break;
            case 'S':
                /* S-register: Sn=v or Sn? - consume the operands, accept. */
                while (i + 1 < u.size() &&
                       (isdigit((unsigned char)u[i + 1]) ||
                        u[i + 1] == '=' || u[i + 1] == '?'))
                    ++i;
                break;
            default:
                break;   /* Q, M, L, X, +/%/\ extensions, digits: accept */
        }
    }

    if (dial)        { SetCarrier(true);  Reply("CONNECT", '1'); }
    else if (hangup) { SetCarrier(false); Reply("OK", '0'); }
    else             { Reply("OK", '0'); }
}

void ModemPersonality::SetCarrier(bool on) {
    online_ = on;
    if (uart_) uart_->SetModemInputs(/*cts=*/true, /*dsr=*/true, /*ri=*/false,
                                     /*dcd=*/on);
    if (on) {
        if (!terminator_ && uart_)
            terminator_ = std::make_unique<PppTerminator>(emu_, *uart_, net_id_);
        if (terminator_) terminator_->Start();
    } else if (terminator_) {
        terminator_->Stop();
    }
}

void ModemPersonality::Reply(const char* verbose_text, char numeric_code) {
    char buf[40];
    if (verbose_) std::snprintf(buf, sizeof buf, "\r\n%s\r\n", verbose_text);
    else          std::snprintf(buf, sizeof buf, "%c\r", numeric_code);
    LOG(Periph, "[Modem] reply %s\n", verbose_text);
    PushStr(buf);
}

void ModemPersonality::PushStr(const char* s) {
    if (uart_) uart_->PushRx(reinterpret_cast<const uint8_t*>(s),
                             std::strlen(s));
}
