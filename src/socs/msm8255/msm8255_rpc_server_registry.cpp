#include "msm8255_rpc_server_registry.h"

#include "msm8255_rpc_server.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

#include <cstdint>
#include <typeinfo>

namespace cerfcore {

bool Msm8255RpcServerRegistry::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

void Msm8255RpcServerRegistry::Register(Msm8255RpcServer* server) {
    uint32_t cids[2] = {server->ServerCid(), 0};
    uint32_t count   = 1;
    if (server->CallbackClientCid(cids[1])) {
        count = 2;
        if (cids[0] == cids[1]) {
            emu_.Get<Fatal>().Die(
                "msm8255 rpc server registry: '%s' names cid %u as both its "
                "server and its callback client",
                typeid(*server).name(), cids[0]);
        }
    }

    for (auto* other : servers_) {
        uint32_t other_cids[2] = {other->ServerCid(), 0};
        uint32_t other_count   = 1;
        if (other->CallbackClientCid(other_cids[1])) other_count = 2;

        for (uint32_t i = 0; i < count; ++i) {
            for (uint32_t j = 0; j < other_count; ++j) {
                if (cids[i] != other_cids[j]) continue;
                emu_.Get<Fatal>().Die(
                    "msm8255 rpc server registry: '%s' and '%s' both claim cid "
                    "%u, and the router addresses one endpoint per cid",
                    typeid(*server).name(), typeid(*other).name(), cids[i]);
            }
        }

        if (server->ServerProg() == other->ServerProg() &&
            server->ServerVers() == other->ServerVers()) {
            emu_.Get<Fatal>().Die(
                "msm8255 rpc server registry: '%s' and '%s' both host prog "
                "0x%08X vers 0x%08X", typeid(*server).name(),
                typeid(*other).name(), server->ServerProg(),
                server->ServerVers());
        }
    }

    servers_.push_back(server);
}

REGISTER_SERVICE(Msm8255RpcServerRegistry);

}
