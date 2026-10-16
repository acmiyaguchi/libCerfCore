#include "guest_stub_host.h"

#include "cerf_injection_region.h"
#include <cerfcore/boot/rom_parser_service.h>
#include "rom_record_layout.h"

#include "../boards/page_table_builder.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>

REGISTER_SERVICE(GuestStubHost);

bool GuestStubHost::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

bool GuestStubHost::LargestVictimSection(uint32_t o32_pa, uint16_t objcnt,
                                         uint32_t& out_va, uint32_t& out_size) {
    auto& mem = emu_.Get<EmulatedMemory>();
    const auto& romhdr = emu_.Get<RomParserService>().Primary().xips[0].toc.romhdr;
    out_va   = 0;
    out_size = 0;
    for (uint32_t i = 0; i < objcnt; ++i) {
        const uint32_t o       = o32_pa + i * kO32RomSize;
        const uint32_t psize   = mem.ReadWord(o + kO32OffPsize);
        const uint32_t dataptr = mem.ReadWord(o + kO32OffDataptr);
        if (psize == 0 || dataptr < romhdr.physfirst) continue;
        if (uint64_t(dataptr) + psize > romhdr.physlast) continue;
        if (psize > out_size) {
            out_size = psize;
            out_va   = dataptr;
        }
    }
    return out_size != 0;
}

const char* GuestStubHost::SquatReject(uint32_t sq_va, uint32_t sq_size,
                                       uint32_t foot_bytes, bool in_place,
                                       uint32_t& out_pa) {
    if (in_place && (sq_va & kRomPageMask) != 0)
        return "largest victim section is not page-aligned (in-place load)";
    if ((sq_va & 3u) != 0)
        return "largest victim section is not 4-byte aligned";
    if (foot_bytes > sq_size)
        return "largest victim section smaller than the stub footprint";

    auto& pt = emu_.Get<PageTableBuilder>();
    out_pa = pt.VaToPa(sq_va);
    for (uint32_t va = sq_va; va - sq_va < foot_bytes; va = (va | kRomPageMask) + 1u) {
        if (pt.VaToPa(va) != out_pa + (va - sq_va))
            return "victim section is not PA-contiguous over the footprint";
    }
    if (!emu_.Get<EmulatedMemory>().CanCopyRange(out_pa, foot_bytes, in_place)) {
        return in_place
            ? "victim section is not inside one writable CERF memory region"
            : "victim section is not inside one CERF memory region";
    }
    return nullptr;
}

GuestStubHostSpan GuestStubHost::Pick(const char* victim_name, uint32_t o32_pa,
                                      uint16_t objcnt, uint32_t foot_bytes,
                                      bool in_place) {
    uint32_t sq_va = 0, sq_size = 0, sq_pa = 0;
    const char* reject = LargestVictimSection(o32_pa, objcnt, sq_va, sq_size)
        ? SquatReject(sq_va, sq_size, foot_bytes, in_place, sq_pa)
        : "no victim section inside ROMHDR physfirst..physlast";

    if (!reject) {
        LOG(GuestAdditions, "%s host = SQUAT: victim section VA 0x%08X PA 0x%08X "
                  "size 0x%X holds footprint 0x%X\n",
            victim_name, sq_va, sq_pa, sq_size, foot_bytes);
        return GuestStubHostSpan{sq_va, sq_pa, sq_size, true};
    }

    LOG(GuestAdditions, "%s host = BAND: squat rejected - %s (victim section "
              "VA 0x%08X size 0x%X, footprint 0x%X)\n",
        victim_name, reject, sq_va, sq_size, foot_bytes);
    auto& region = emu_.Get<CerfInjectionRegion>();
    if (uint64_t(band_next_) + foot_bytes > region.BandSize()) {
        LOG(Caution, "%s stub needs 0x%X bytes at band offset 0x%X; band 0x%X - "
                "raise kInjectionBandSize in cerf_virt_addr_map.h\n",
            victim_name, foot_bytes, band_next_, region.BandSize());
        CerfFatalExit();
    }
    GuestStubHostSpan span{region.BandVaBase() + band_next_,
                           region.BandPaBase() + band_next_,
                           region.BandSize() - band_next_, false};
    band_next_ = AlignRomPage(band_next_ + foot_bytes);
    return span;
}
