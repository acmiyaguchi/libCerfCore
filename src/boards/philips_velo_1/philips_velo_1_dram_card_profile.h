#pragma once

#include "philips_velo_1_dram_16mb_id.h"
#include "philips_velo_1_dram_1mb_id.h"
#include "philips_velo_1_dram_2mb_id.h"
#include "philips_velo_1_dram_4mb_id.h"
#include "philips_velo_1_dram_8mb_id.h"

#include <cstdint>
#include <string_view>

namespace cerfcore {

struct PhilipsVelo1DramCardProfile {
    std::string_view board_id;
    uint32_t         megabytes;
    uint8_t          row_bits;
    uint8_t          column_bits;

    uint32_t Bytes() const { return megabytes << 20; }
};

/* (row, column) address-bit pairs are entries of asc_9F40A904 @0x9F40A904, the table
   philips_velo_1_ce1 nk.exe sub_9F42448C @0x9F42448C matches the card's ID EEPROM
   against. */
inline constexpr PhilipsVelo1DramCardProfile kPhilipsVelo1DramCards[] = {
    { BoardId::PhilipsVelo1Dram1mb,   1u, 10u,  9u },
    { BoardId::PhilipsVelo1Dram2mb,   2u, 12u,  8u },
    { BoardId::PhilipsVelo1Dram4mb,   4u, 11u, 10u },
    { BoardId::PhilipsVelo1Dram8mb,   8u, 12u, 10u },
    { BoardId::PhilipsVelo1Dram16mb, 16u, 12u, 11u },
};

constexpr bool PhilipsVelo1DramCardGeometryHolds() {
    for (const PhilipsVelo1DramCardProfile& p : kPhilipsVelo1DramCards) {
        if ((uint64_t{2} << (p.row_bits + p.column_bits)) != (uint64_t{p.megabytes} << 20)) {
            return false;
        }
    }
    return true;
}
static_assert(PhilipsVelo1DramCardGeometryHolds());

inline const PhilipsVelo1DramCardProfile* FindPhilipsVelo1DramCard(std::string_view board_id) {
    for (const PhilipsVelo1DramCardProfile& p : kPhilipsVelo1DramCards) {
        if (p.board_id == board_id) return &p;
    }
    return nullptr;
}

}
