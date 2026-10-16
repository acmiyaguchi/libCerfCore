#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

class Msm8255RpcServer : public Service {
public:
    using Service::Service;

    virtual void SaveState(StateWriter& w);
    virtual void RestoreState(StateReader& r);

    virtual uint32_t ServerProg() const = 0;
    virtual uint32_t ServerVers() const = 0;
    virtual uint32_t ServerCid() const  = 0;

    virtual uint32_t AnswerCall(uint32_t in_pa, uint32_t size, uint32_t out_pa,
                                uint32_t out_cap, uint32_t self_pid,
                                uint32_t peer_pid, uint32_t peer_cid) = 0;

    virtual bool CallbackClientCid(uint32_t& cid) const;

    virtual uint32_t ConsumeCallbackReply(uint32_t in_pa, uint32_t size,
                                          uint32_t out_pa, uint32_t out_cap);
};

}
