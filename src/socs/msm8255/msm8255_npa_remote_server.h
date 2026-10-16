#pragma once

#include "msm8255_rpc_server.h"

#include <cstdint>

class StateReader;
class StateWriter;

class Msm8255NpaRemoteServer : public Msm8255RpcServer {
public:
    using Msm8255RpcServer::Msm8255RpcServer;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t ServerProg() const override;
    uint32_t ServerVers() const override;
    uint32_t ServerCid() const override;
    bool CallbackClientCid(uint32_t& cid) const override;

    uint32_t AnswerCall(uint32_t in_pa, uint32_t size, uint32_t out_pa,
                        uint32_t out_cap, uint32_t self_pid, uint32_t peer_pid,
                        uint32_t peer_cid) override;
    uint32_t ConsumeCallbackReply(uint32_t in_pa, uint32_t size,
                                  uint32_t out_pa, uint32_t out_cap) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    void ReadDefineNodeArgs(uint32_t body, uint32_t size, uint32_t& callback,
                            uint32_t& object);
    void ReadDefineResourceArgs(uint32_t body, uint32_t size,
                                uint32_t& callback, uint32_t& object);
    void ReadCreateClientArgs(uint32_t body, uint32_t size, uint32_t& type,
                              uint32_t& supplied, uint32_t& resource);
    uint32_t AnswerCreateClient(uint32_t body, uint32_t size, uint32_t out_pa,
                                uint32_t out_cap, uint32_t self_pid,
                                uint32_t peer_pid, uint32_t peer_cid,
                                uint32_t xid);
    uint32_t AnswerIssueRequest(uint32_t body, uint32_t size, uint32_t out_pa,
                                uint32_t out_cap, uint32_t self_pid,
                                uint32_t peer_pid, uint32_t peer_cid,
                                uint32_t xid);
    uint32_t EmitCallback(uint32_t out_pa, uint32_t out_cap, uint32_t reserved,
                          uint32_t self_pid, uint32_t proc, uint32_t cb_index,
                          uint32_t node);

    uint32_t next_xid_           = 1;
    uint32_t cb_xid_             = 0;
    uint32_t cb_proc_            = 0;
    bool     cb_outstanding_     = false;
};
