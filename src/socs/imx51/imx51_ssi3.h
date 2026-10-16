#pragma once

#include "imx51_ssi_impl.h"

/* SSI3 @ 0x83FE8000 (MCIMX51RM Table 2-1; wavedev2_cs42448.dll BSPAudioInit
   sub_C0CB65B0 MmMapIoSpace). */
class Imx51Ssi3 : public cerf_imx51_ssi_detail::Imx51SsiImpl<0x83FE8000u> {
    using Imx51SsiImpl::Imx51SsiImpl;
};
