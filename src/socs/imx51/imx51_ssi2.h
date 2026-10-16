#pragma once

#include "imx51_ssi_impl.h"

/* SSI2 @ 0x70014000 (MCIMX51RM Table 2-1 SPBA0 slot; wavedev2_cs42448.dll BSPAudioInit
   sub_C0CB65B0 MmMapIoSpace). */
class Imx51Ssi2 : public cerf_imx51_ssi_detail::Imx51SsiImpl<0x70014000u> {
    using Imx51SsiImpl::Imx51SsiImpl;
};
