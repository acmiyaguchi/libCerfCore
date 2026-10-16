#pragma once

#include <cerfcore/core/service.h>
#include "trace_manager.h"

#include <cstddef>
#include <string>
#include <string_view>

/* Single funnel for guest OS debug text. Every producer - live SoC/board
   UART/serial TX, and hooks on a nulled OEM debug sink - routes finished
   lines here, which emits them to the always-on Nkdbg log channel and the
   HwScreen debug console. */
class KernelDebugSink : public Service {
public:
    using Service::Service;

    /* Emit one finished line. `source` is an optional short tag shown in the
       log (e.g. "UART1"); pass {} for none. `to_screen` mirrors it to
       HwScreen. Empty lines are dropped. */
    void EmitLine(std::string_view line,
                  std::string_view source = {},
                  bool to_screen = true);

    /* Char-stream accumulator for the common CRLF debug pattern: append a
       printable `ch` to caller-owned `buf`, flush as a line on '\n' (drop
       '\r'), hex-escape non-printables, and force-flush at `cap` bytes. The
       buffer lives in the caller, so concurrent producers never share state. */
    void EmitChar(char ch, std::string& buf,
                  std::string_view source = {},
                  bool to_screen = true,
                  size_t cap = 256);

    /* Read a NUL-terminated guest wide string at `va` (printable ASCII only)
       and EmitLine it. */
    void EmitWideStringAt(const TraceContext& c, uint32_t va,
                          std::string_view source = {});
};
