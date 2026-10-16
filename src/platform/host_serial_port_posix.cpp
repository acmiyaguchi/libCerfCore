#include "host_serial_port.h"

#include "host_unimplemented.h"

namespace host_platform {

struct HostSerialPort::Impl {};

HostSerialPort::HostSerialPort() : impl_(std::make_unique<Impl>()) {}

HostSerialPort::~HostSerialPort() = default;

bool HostSerialPort::Open(const std::string&, uint32_t&) {
    NoHostImplementation("HostSerialPort::Open");
}
void HostSerialPort::StartIo(uint32_t, uint32_t) { NoHostImplementation("HostSerialPort::StartIo"); }
void HostSerialPort::Close() { NoHostImplementation("HostSerialPort::Close"); }
bool HostSerialPort::IsOpen() const { NoHostImplementation("HostSerialPort::IsOpen"); }

SerialConfigureResult HostSerialPort::Configure(const SerialPortSettings&, uint32_t&) {
    NoHostImplementation("HostSerialPort::Configure");
}
void HostSerialPort::SetControlLines(bool, bool) {
    NoHostImplementation("HostSerialPort::SetControlLines");
}
bool HostSerialPort::ModemLines(uint8_t&) { NoHostImplementation("HostSerialPort::ModemLines"); }

SerialIoResult HostSerialPort::Read(uint8_t*, uint32_t, uint32_t&, uint32_t&) {
    NoHostImplementation("HostSerialPort::Read");
}
SerialIoResult HostSerialPort::WriteByte(uint8_t, uint32_t&, uint32_t&) {
    NoHostImplementation("HostSerialPort::WriteByte");
}
bool HostSerialPort::PaceWait(int64_t) { NoHostImplementation("HostSerialPort::PaceWait"); }

void HostSerialPort::RequestStop() { NoHostImplementation("HostSerialPort::RequestStop"); }
bool HostSerialPort::WaitStopped(uint32_t) { NoHostImplementation("HostSerialPort::WaitStopped"); }

std::vector<std::string> EnumerateSerialPorts() { NoHostImplementation("EnumerateSerialPorts"); }

}
