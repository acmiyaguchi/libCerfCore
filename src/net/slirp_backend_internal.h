#pragma once
/* Internal helpers shared between slirp_backend.cpp and its
   sibling TUs (slirp_classify_frame.cpp, slirp_icmp_echo.cpp,
   slirp_dns_aaaa_strip.cpp). Not part of the public network API -
   callers outside cerf/net/ must go through SlirpBackend instead. */

#include <cstddef>
#include <cstdint>

/* Classify a raw Ethernet frame into a short ASCII tag for [NET]
   log lines. Output is always NUL-terminated within `out_len`. */
void ClassifyFrame(const uint8_t* frame, std::size_t len,
                   char* out, std::size_t out_len);
