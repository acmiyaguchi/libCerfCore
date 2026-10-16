#pragma once

#include "imx31_ssi_impl.h"

namespace cerfcore {

/* MCIMX31RM Table 45-4 (page 45-20): SSI2 at 0x5001_4000. */
class Imx31Ssi2 : public cerf_imx31_ssi_detail::Imx31SsiImpl<0x50014000u> {
public:
    using Imx31SsiImpl::Imx31SsiImpl;
};

}
