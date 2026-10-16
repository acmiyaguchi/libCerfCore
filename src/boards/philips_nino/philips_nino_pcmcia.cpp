#include "../../peripherals/ite_it8368/ite_it8368_socket_host.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/pr31x00/pr31x00_io.h"
#include <cerfcore/boards/board_context.h>
#include "philips_nino_family_id.h"

namespace cerfcore {

namespace {

constexpr uint32_t kIt8368IntMfioPin = 2;

class PhilipsNinoPcmcia : public IteIt8368SocketHost {
public:
    explicit PhilipsNinoPcmcia(CerfEmulator& emu)
        : IteIt8368SocketHost(emu, "CompactFlash slot") {}

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::PhilipsNino;
    }

    void OnIt8368IntLevel(bool asserted) override {
        emu_.Get<Pr31x00Io>().DriveMfioInput(kIt8368IntMfioPin, asserted);
    }
};

}

REGISTER_SERVICE(PhilipsNinoPcmcia);

}
