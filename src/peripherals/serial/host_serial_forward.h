#pragma once

#include "serial_endpoint.h"
#include "serial_line.h"
#include "../../platform/host_serial_port.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cerfcore {

class CerfEmulator;

class HostSerialForward : public SerialEndpoint {
public:
    HostSerialForward(std::string host_port, CerfEmulator& emu);
    ~HostSerialForward() override;

    void SetOnBridgeDead(std::function<void()> cb) { on_dead_ = std::move(cb); }

    void OnGuestTx(const uint8_t* data, size_t n) override;
    void OnControlLines(bool dtr, bool rts) override;
    void OnOpen()  override;
    void OnClose() override;
    void ResendModemInputs() override;

private:
    void ReaderLoop();
    void WriterLoop();
    void ApplyLineConfig(const SerialLine::LineConfig& c);
    void StopThreads();
    void ReportDead(const std::string& detail);

    bool WithGuestAccess(const std::function<void()>& touch);

    CerfEmulator&               emu_;
    std::string                 port_name_;
    host_platform::HostSerialPort port_;
    std::atomic<bool>           running_{false};
    std::atomic<bool>           dead_{false};

    std::function<void()> on_dead_;

    std::thread reader_;
    std::thread writer_;

    std::mutex              tx_mu_;
    std::condition_variable tx_cv_;
    std::vector<uint8_t>    tx_buf_;

    std::mutex ctl_mu_;
    bool       dtr_ = false;
    bool       rts_ = false;

    static constexpr uint8_t kModemLinesUnpushed = 0xFFu;
    std::atomic<uint8_t> last_ms_{kModemLinesUnpushed};
};

}
