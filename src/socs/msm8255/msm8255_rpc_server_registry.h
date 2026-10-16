#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <vector>

class Msm8255RpcServer;

class Msm8255RpcServerRegistry : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    void Register(Msm8255RpcServer* server);

    const std::vector<Msm8255RpcServer*>& Servers() const { return servers_; }

private:
    std::vector<Msm8255RpcServer*> servers_;
};
