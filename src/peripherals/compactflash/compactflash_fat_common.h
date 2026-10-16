#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cerfcore {

namespace cf_fat {

std::string BaseName(const std::string& path);

struct ShortName {
    uint8_t sfn[11];
    uint8_t ntres;      /* NT case flags at dir offset 12 (0x08 base, 0x10 ext) */
    bool    needs_lfn;
};

/* Natural 8.3 short name (+ case flags, no LFN) when the name fits 8.3; a
   "~mangle_index" mangled short name (+ LFN) otherwise. */
ShortName MakeShortName(const std::u16string& name, int mangle_index);

/* Forced "~mangle_index" mangle, for a natural short-name collision fallback. */
void MakeMangledSfn(const std::u16string& name, int mangle_index, uint8_t out[11]);

uint8_t LfnChecksum(const uint8_t sfn[11]);
uint32_t LfnSlotCount(const std::u16string& name);

uint8_t* EmitFileDir(uint8_t* dir, const std::u16string& name, const uint8_t sfn[11],
                     uint32_t lfn_count, uint8_t ntres, uint32_t first_clus, uint32_t size);

struct FileEntry {
    std::u16string       name;
    std::vector<uint8_t> data;
    uint8_t              sfn[11]    = {};
    uint8_t              ntres      = 0;
    uint32_t             lfn_count  = 0;
    uint32_t             first_clus = 0;
    uint32_t             clusters   = 0;
};

std::vector<FileEntry> CollectEntries(const std::vector<std::string>& files,
                                      uint32_t cluster_bytes);

}  /* namespace cf_fat */

}
