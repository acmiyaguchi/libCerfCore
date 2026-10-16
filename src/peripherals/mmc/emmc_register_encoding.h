#pragma once

#include <cerfcore/core/sd_card_cid.h>

#include <cstdint>

struct EmmcCsdFields {
    uint8_t  csd_structure;
    uint8_t  spec_vers;
    uint8_t  taac;
    uint8_t  nsac;
    uint8_t  tran_speed;
    uint16_t ccc;
    uint8_t  read_bl_len;
    uint16_t c_size;
    uint8_t  vdd_r_curr_min;
    uint8_t  vdd_r_curr_max;
    uint8_t  vdd_w_curr_min;
    uint8_t  vdd_w_curr_max;
    uint8_t  c_size_mult;
    uint8_t  erase_grp_size;
    uint8_t  erase_grp_mult;
    uint8_t  wp_grp_size;
    uint8_t  wp_grp_enable;
    uint8_t  r2w_factor;
    uint8_t  write_bl_len;
};

void EncodeEmmcCid(const SdCardCid& cid, uint32_t out[4]);

void EncodeEmmcCsd(const EmmcCsdFields& csd, uint32_t out[4]);
