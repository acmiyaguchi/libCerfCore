#pragma once

#include "../mmc/mmc_card.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

/* SD Specifications Part 1: Physical Layer Simplified Specification Version 2.00. */
class SdCardBase : public MmcCard {
public:
    using MmcCard::MmcCard;

    MmcCommandResult Command(uint8_t index, uint32_t argument,
                             uint32_t response[4]) override;

    const std::vector<uint8_t>& ReadData() const override { return read_data_; }

    void NextBlock() override;

    void EndDataPhase() override;

    void ReceiveBlock(const uint8_t* data, uint32_t bytes) override;

    void Reset() override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

protected:
    virtual uint32_t SectorCount() const = 0;
    virtual void ReadBlock(uint32_t sector, uint8_t* out) = 0;
    virtual void WriteBlock(uint32_t sector, const uint8_t* data) = 0;

private:
    uint32_t StatusWord(uint32_t state) const;

    uint32_t             state_       = 0u;
    bool                 app_cmd_     = false;
    uint16_t             rca_         = 0u;
    uint32_t             block_len_   = 512u;
    bool                 multi_read_  = false;
    bool                 multi_write_ = false;
    uint32_t             next_sector_ = 0u;
    std::vector<uint8_t> read_data_;
};

}
