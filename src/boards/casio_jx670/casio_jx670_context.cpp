#include <cerfcore/boards/board_context.h>

#include "casio_jx670_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace {

class CasioJx670Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::CasioJx670; }

    /* VR4131 UM Fig 3-1: PA 0x20000000-0xFFFFFFFF mirrors 0x00000000-0x1FFFFFFF. */
    uint32_t GuestAdditionsWindowBase() const override { return 0x04000000u; }
    /* casio_cassiopeia_em500_ppc2000 nk_main_kernel.exe sub_9F032B60 drives the
       companion ASIC from kseg1 0xAA000000 (PA 0x0A000000). */
    uint32_t GuestAdditionsWindowSize() const override { return 0x06000000u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(CasioJx670Context, BoardContext);
