#pragma once

#include <cerfcore/core/service.h>
#include "rom_record_layout.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

class PeImage;

/* CE2.11 does not re-bind an injected XIP ROM module's imports at load (it trusts
   romimage pre-binding), so its IAT keeps raw 0x80000000|ordinal hints and the
   first imported call branches there (coredll VirtualAlloc = ord 524 ->
   0x8000020C). This resolves them and writes runtime addresses into the IAT. */
class CeImportBinder : public Service {
public:
    using Service::Service;

    /* `bytes` = the injected module's PE image; `pe` = its parsed headers;
       `layout` = the ROM's e32_rom layout (to read target modules' export dirs).
       Reads the guest ROM as placed, so call after ROM placement. */
    void BindImports(std::vector<uint8_t>& bytes, const PeImage& pe,
                     const E32RomLayout& layout);

private:
    struct GuestExportDir {
        uint32_t vbase    = 0;   /* target module exec vbase (resolved-value base) */
        uint32_t exp_rva  = 0;   /* IMAGE_EXPORT_DIRECTORY rva (e32_unit[EXP].rva) */
        uint32_t exp_size = 0;   /* export dir size (for the forwarder-range test) */
        uint32_t o32_pa   = 0;   /* PA of the module's o32_rom array (RVA->ROM map) */
        uint32_t objcnt   = 0;   /* o32 section count                              */
    };

    GuestExportDir LocateModuleExports(const char* dll, const E32RomLayout& layout);
    /* A ROM module's section bytes live at its per-section o32 dataptr (a ROM KVA),
       NOT at exec_vbase+rva (the low FCSE exec view the OAT can't translate). */
    uint32_t MapRvaToPa(const GuestExportDir& ed, uint32_t rva);
    uint32_t ResolveOrdinal(const GuestExportDir& ed, uint32_t ordinal,
                            const char* dll);
    uint32_t ResolveName(const GuestExportDir& ed, const char* fn, const char* dll);
    bool     GuestAsciiEquals(const GuestExportDir& ed, uint32_t name_rva,
                              const char* s);
};

}
