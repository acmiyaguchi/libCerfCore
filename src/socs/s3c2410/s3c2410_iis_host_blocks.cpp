#include "s3c2410_iis_host_blocks.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

#include <cstring>

namespace cerfcore {

void S3C2410IisHostBlocks::Push(uint16_t sample, Completed& out) {
    cerf::le::Put16(block_.data() + fill_, sample);
    fill_ += sizeof(sample);
    if (fill_ == kBlockBytes) Complete(out);
}

void S3C2410IisHostBlocks::Pad(Completed& out) {
    if (fill_ == 0u) return;
    std::memset(block_.data() + fill_, 0, kBlockBytes - fill_);
    fill_ = kBlockBytes;
    Complete(out);
}

void S3C2410IisHostBlocks::Complete(Completed& out) {
    if (out.count == 2u)
        emu_.Get<Fatal>().Die("[S3C2410Iis] a third host block completed in one "
                              "catch-up (host_fill=%u)", fill_);
    std::memcpy(out.block[out.count++], block_.data(), kBlockBytes);
    fill_ = 0u;
}

}
