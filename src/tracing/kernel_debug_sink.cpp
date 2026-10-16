#include "kernel_debug_sink.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/host/hw_screen.h>

#include <cstdio>

namespace cerfcore {

REGISTER_SERVICE(KernelDebugSink);

void KernelDebugSink::EmitLine(std::string_view line,
                               std::string_view source,
                               bool to_screen) {
    if (line.empty()) return;
    if (source.empty())
        LOG(Nkdbg, "%.*s\n", (int)line.size(), line.data());
    else
        LOG(Nkdbg, "%.*s: %.*s\n",
            (int)source.size(), source.data(),
            (int)line.size(), line.data());
    if (to_screen) emu_.Get<HwScreen>().AddLine(line);
}

void KernelDebugSink::EmitChar(char ch, std::string& buf,
                               std::string_view source,
                               bool to_screen, size_t cap) {
    if (ch == '\n') { EmitLine(buf, source, to_screen); buf.clear(); return; }
    if (ch == '\r') return;  /* CE emits CRLF - drop CR, flush on LF. */
    const unsigned char uc = (unsigned char)ch;
    if (uc >= 0x20 && uc < 0x7F) {
        buf.push_back(ch);
    } else {
        char esc[8];
        std::snprintf(esc, sizeof esc, "\\x%02X", uc);
        buf.append(esc);
    }
    if (buf.size() >= cap) { EmitLine(buf, source, to_screen); buf.clear(); }
}

void KernelDebugSink::EmitWideStringAt(const TraceContext& c, uint32_t va,
                                       std::string_view source) {
    std::string s;
    for (uint32_t i = 0; i < 1024u; ++i) {
        auto w = c.ReadVa16(va + 2u * i);
        if (!w || *w == 0) break;
        if (*w >= 0x20 && *w < 0x7F) s.push_back(char(*w));
    }
    EmitLine(s, source);
}

}
