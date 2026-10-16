#include "msm8255_rpc_server.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

#include <typeinfo>

void Msm8255RpcServer::SaveState(StateWriter& w) { (void)w; }

void Msm8255RpcServer::RestoreState(StateReader& r) { (void)r; }

bool Msm8255RpcServer::CallbackClientCid(uint32_t& cid) const {
    (void)cid;
    return false;
}

uint32_t Msm8255RpcServer::ConsumeCallbackReply(uint32_t in_pa, uint32_t size,
                                                uint32_t out_pa,
                                                uint32_t out_cap) {
    (void)in_pa;
    (void)out_pa;
    (void)out_cap;
    emu_.Get<Fatal>().Die(
        "Service '%s': a %u-byte callback reply reached a server that names a "
        "callback client and models no reply for it",
        typeid(*this).name(), size);
}
