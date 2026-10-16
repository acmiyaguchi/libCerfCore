#pragma once

#include "imx31_ssi_impl.h"

/* MCIMX31RM Table 45-4 (page 45-20): SSI1 at 0x43FA_0000. */
class Imx31Ssi1 : public cerf_imx31_ssi_detail::Imx31SsiImpl<0x43FA0000u> {
public:
    using Imx31SsiImpl::Imx31SsiImpl;
};
