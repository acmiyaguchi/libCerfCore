#include "host_serial_port.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cerfcore/platform/wide_text_win32.h>
#include "host_clock.h"
#include "host_wait_timer.h"

namespace host_platform {

struct HostSerialPort::Impl {
    HANDLE handle      = nullptr;
    HANDLE read_event  = nullptr;
    HANDLE write_event = nullptr;
    HANDLE stop_event  = nullptr;
    HostWaitTimer pace_timer;
};

HostSerialPort::HostSerialPort() : impl_(std::make_unique<Impl>()) {}

HostSerialPort::~HostSerialPort() { Close(); }

bool HostSerialPort::Open(const std::string& name, uint32_t& error) {
    const std::wstring path = L"\\\\.\\" + Utf8ToWide(name.c_str());
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return false;
    }
    impl_->handle = h;
    return true;
}

void HostSerialPort::StartIo(uint32_t read_timeout_ms, uint32_t write_timeout_ms) {
    COMMTIMEOUTS to{};
    to.ReadIntervalTimeout        = MAXDWORD;
    to.ReadTotalTimeoutMultiplier = MAXDWORD;
    to.ReadTotalTimeoutConstant   = read_timeout_ms;
    to.WriteTotalTimeoutConstant  = write_timeout_ms;
    SetCommTimeouts(impl_->handle, &to);

    impl_->read_event  = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    impl_->write_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    impl_->stop_event  = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    impl_->pace_timer.Create();
}

void HostSerialPort::Close() {
    Impl& p = *impl_;
    if (p.handle)      { CloseHandle(p.handle);      p.handle      = nullptr; }
    if (p.read_event)  { CloseHandle(p.read_event);  p.read_event  = nullptr; }
    if (p.write_event) { CloseHandle(p.write_event); p.write_event = nullptr; }
    if (p.stop_event)  { CloseHandle(p.stop_event);  p.stop_event  = nullptr; }
    p.pace_timer.Close();
}

bool HostSerialPort::IsOpen() const { return impl_->handle != nullptr; }

SerialConfigureResult HostSerialPort::Configure(const SerialPortSettings& s, uint32_t& error) {
    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(impl_->handle, &dcb)) return SerialConfigureResult::kStateUnreadable;

    dcb.BaudRate = s.baud;
    dcb.ByteSize = s.data_bits;
    switch (s.parity) {
        case SerialPortSettings::Parity::kNone:  dcb.Parity = NOPARITY;    break;
        case SerialPortSettings::Parity::kOdd:   dcb.Parity = ODDPARITY;   break;
        case SerialPortSettings::Parity::kEven:  dcb.Parity = EVENPARITY;  break;
        case SerialPortSettings::Parity::kMark:  dcb.Parity = MARKPARITY;  break;
        case SerialPortSettings::Parity::kSpace: dcb.Parity = SPACEPARITY; break;
    }
    switch (s.stop) {
        case SerialPortSettings::StopBits::kOne:          dcb.StopBits = ONESTOPBIT;   break;
        case SerialPortSettings::StopBits::kOnePointFive: dcb.StopBits = ONE5STOPBITS; break;
        case SerialPortSettings::StopBits::kTwo:          dcb.StopBits = TWOSTOPBITS;  break;
    }
    dcb.fBinary         = TRUE;
    dcb.fParity         = (s.parity != SerialPortSettings::Parity::kNone);
    dcb.fOutxCtsFlow    = FALSE;
    dcb.fOutxDsrFlow    = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fDtrControl     = s.dtr ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
    dcb.fRtsControl     = s.rts ? RTS_CONTROL_ENABLE : RTS_CONTROL_DISABLE;
    dcb.fInX            = FALSE;
    dcb.fOutX           = FALSE;
    dcb.fNull           = FALSE;
    dcb.fAbortOnError   = FALSE;
    if (!SetCommState(impl_->handle, &dcb)) {
        error = GetLastError();
        return SerialConfigureResult::kRejected;
    }
    return SerialConfigureResult::kApplied;
}

void HostSerialPort::SetControlLines(bool dtr, bool rts) {
    EscapeCommFunction(impl_->handle, dtr ? SETDTR : CLRDTR);
    EscapeCommFunction(impl_->handle, rts ? SETRTS : CLRRTS);
}

bool HostSerialPort::ModemLines(uint8_t& lines) {
    DWORD ms = 0;
    if (!GetCommModemStatus(impl_->handle, &ms)) return false;
    lines = 0;
    if (ms & MS_CTS_ON)  lines |= kModemCts;
    if (ms & MS_DSR_ON)  lines |= kModemDsr;
    if (ms & MS_RING_ON) lines |= kModemRing;
    if (ms & MS_RLSD_ON) lines |= kModemDcd;
    return true;
}

SerialIoResult HostSerialPort::Read(uint8_t* buf, uint32_t size, uint32_t& got,
                                    uint32_t& error) {
    OVERLAPPED ov{};
    ov.hEvent = impl_->read_event;
    DWORD n = 0;
    ResetEvent(impl_->read_event);
    if (!ReadFile(impl_->handle, buf, size, &n, &ov)) {
        const DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            error = err;
            return SerialIoResult::kFailed;
        }
        const HANDLE waits[2] = { impl_->read_event, impl_->stop_event };
        if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0) {
            CancelIoEx(impl_->handle, &ov);
            return SerialIoResult::kStopped;
        }
        if (!GetOverlappedResult(impl_->handle, &ov, &n, FALSE)) n = 0;
    }
    got = n;
    return SerialIoResult::kDone;
}

SerialIoResult HostSerialPort::WriteByte(uint8_t value, uint32_t& wrote, uint32_t& error) {
    OVERLAPPED ov{};
    ov.hEvent = impl_->write_event;
    DWORD n = 0;
    ResetEvent(impl_->write_event);
    if (!WriteFile(impl_->handle, &value, 1, &n, &ov)) {
        const DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            error = err;
            return SerialIoResult::kFailed;
        }
        const HANDLE waits[2] = { impl_->write_event, impl_->stop_event };
        if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0) {
            CancelIoEx(impl_->handle, &ov);
            return SerialIoResult::kStopped;
        }
        if (!GetOverlappedResult(impl_->handle, &ov, &n, FALSE)) {
            error = GetLastError();
            return SerialIoResult::kFailed;
        }
    }
    wrote = n;
    return SerialIoResult::kDone;
}

bool HostSerialPort::PaceWait(int64_t monotonic_ticks) {
    if (!impl_->pace_timer.Created()) return true;
    const int64_t freq = MonotonicFrequency();
    impl_->pace_timer.Arm(monotonic_ticks / freq * 1000000000 +
                          monotonic_ticks % freq * 1000000000 / freq);
    return impl_->pace_timer.WaitOrEvent(impl_->stop_event);
}

void HostSerialPort::RequestStop() {
    if (impl_->stop_event) SetEvent(impl_->stop_event);
}

bool HostSerialPort::WaitStopped(uint32_t timeout_ms) {
    return WaitForSingleObject(impl_->stop_event, timeout_ms) == WAIT_OBJECT_0;
}

std::vector<std::string> EnumerateSerialPorts() {
    std::vector<std::string> ports;
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0,
                      KEY_READ, &key) != ERROR_SUCCESS)
        return ports;
    for (DWORD i = 0;; ++i) {
        wchar_t name[256];
        wchar_t data[256];
        DWORD nlen = 256, dlen = sizeof(data), type = 0;
        const LONG rc = RegEnumValueW(key, i, name, &nlen, nullptr, &type,
                                      reinterpret_cast<LPBYTE>(data), &dlen);
        if (rc == ERROR_NO_MORE_ITEMS) break;
        if (rc == ERROR_SUCCESS && type == REG_SZ) {
            data[(sizeof(data) / sizeof(wchar_t)) - 1] = L'\0';
            ports.push_back(WideToUtf8(data));
        }
    }
    RegCloseKey(key);
    return ports;
}

}
