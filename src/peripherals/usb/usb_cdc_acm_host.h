#pragma once

#include "../serial/host_serial_forward.h"
#include "../serial/serial_line.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cerfcore {

class CerfEmulator;
class StateReader;
class StateWriter;

/* USB CDC 1.2 section 3.4.1; PSTN 1.2 section 3.2.2.1 Abstract Control Model. */
class UsbCdcAcmHost : public SerialLine {
public:
    struct Result {
        uint32_t moved;
        bool     short_packet;
    };
    using Transact = std::function<bool(uint32_t token, uint8_t* data, uint32_t length, Result& out)>;

    explicit UsbCdcAcmHost(CerfEmulator& emu) : emu_(emu) {}
    UsbCdcAcmHost(const UsbCdcAcmHost&)            = delete;
    UsbCdcAcmHost& operator=(const UsbCdcAcmHost&) = delete;

    /* PSTN 1.2 section 6.3.11 Table 17. */
    void SetLineCoding(const uint8_t* coding);
    void Start(uint32_t in_token, uint32_t out_token, uint32_t max_packet);
    void Open(const std::string& host_path);
    void Stop();
    bool Step(const Transact& port);
    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

    void       PushRx(const uint8_t* data, size_t n) override;
    bool       RxEmpty() const override;
    void       SetRxDrainCallback(RxDrainFn cb) override;
    void       SetModemInputs(bool cts, bool dsr, bool ri, bool dcd) override;
    LineConfig GetLineConfig() const override;
    void       SetLineConfigCallback(LineConfigFn cb) override;

private:
    void Close();

    CerfEmulator&                      emu_;
    std::unique_ptr<HostSerialForward> forward_;
    uint32_t                           in_token_   = 0;
    uint32_t                           out_token_  = 0;
    uint32_t                           max_packet_ = 0;
    uint8_t                            coding_[7]  = {};
    std::atomic<uint64_t>              generation_{0};
    std::shared_ptr<std::atomic<uint64_t>> dead_generation_ = std::make_shared<std::atomic<uint64_t>>(0u);
    mutable std::mutex                 mu_;
    std::deque<uint8_t>                to_device_;
    std::vector<uint8_t>               in_;
    RxDrainFn                          on_drain_;
    LineConfigFn                       on_line_;
};

}
