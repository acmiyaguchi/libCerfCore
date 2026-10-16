#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace host_platform {

struct SerialPortSettings {
    enum class Parity : uint8_t { kNone, kOdd, kEven, kMark, kSpace };
    enum class StopBits : uint8_t { kOne, kOnePointFive, kTwo };

    uint32_t baud      = 0;
    uint8_t  data_bits = 8;
    Parity   parity    = Parity::kNone;
    StopBits stop      = StopBits::kOne;
    bool     dtr       = false;
    bool     rts       = false;
};

enum ModemLine : uint8_t {
    kModemCts  = 1 << 0,
    kModemDsr  = 1 << 1,
    kModemRing = 1 << 2,
    kModemDcd  = 1 << 3,
};

enum class SerialConfigureResult { kApplied, kStateUnreadable, kRejected };
enum class SerialIoResult { kDone, kStopped, kFailed };

class HostSerialPort {
public:
    HostSerialPort();
    ~HostSerialPort();
    HostSerialPort(const HostSerialPort&)            = delete;
    HostSerialPort& operator=(const HostSerialPort&) = delete;

    bool Open(const std::string& name, uint32_t& error);
    void StartIo(uint32_t read_timeout_ms, uint32_t write_timeout_ms);
    void Close();
    bool IsOpen() const;

    SerialConfigureResult Configure(const SerialPortSettings& settings, uint32_t& error);
    void SetControlLines(bool dtr, bool rts);
    bool ModemLines(uint8_t& lines);

    SerialIoResult Read(uint8_t* buf, uint32_t size, uint32_t& got, uint32_t& error);
    SerialIoResult WriteByte(uint8_t value, uint32_t& wrote, uint32_t& error);
    bool PaceWait(int64_t monotonic_ticks);

    void RequestStop();
    bool WaitStopped(uint32_t timeout_ms);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::vector<std::string> EnumerateSerialPorts();

}
