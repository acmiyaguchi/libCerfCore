#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

class Msm8255RpcRouterPeer : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t Answer(uint32_t in_pa, uint32_t in_avail, uint32_t out_pa,
                    uint32_t out_cap, uint32_t& consumed);

    void WriteHeader(uint32_t out_pa, uint32_t type, uint32_t src_pid,
                     uint32_t src_cid, uint32_t size, uint32_t dst_pid,
                     uint32_t dst_cid);
    uint32_t NextPacmark(uint32_t body_bytes);
    void ValidatePacmark(uint32_t pacmark, uint32_t body_bytes);
    uint32_t AnswerConfirmRx(uint32_t in_pa, uint32_t out_pa, uint32_t out_cap,
                             uint32_t reserved);
    bool AnnouncedServer(uint32_t& pid, uint32_t& cid) const;

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    void WriteCtrlHeader(uint32_t out_pa, uint32_t cmd, uint32_t self_pid,
                         uint32_t peer_pid);
    void WriteCtrlMsg(uint32_t out_pa, uint32_t cmd, uint32_t self_pid,
                      uint32_t peer_pid, uint32_t prog, uint32_t vers,
                      uint32_t srv_pid, uint32_t srv_cid);
    void WriteResumeTx(uint32_t out_pa, uint32_t self_pid, uint32_t peer_pid,
                       uint32_t cli_pid, uint32_t cli_cid);
    void RecordAnnouncedServer(uint32_t pid, uint32_t cid);

    uint32_t next_mid_ = 1;

    uint32_t srv_pid_   = 0;
    uint32_t srv_cid_   = 0;
    bool     srv_known_ = false;
};

}
