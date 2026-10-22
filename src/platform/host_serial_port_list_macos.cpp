#include "host_serial_port_list.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/serial/IOSerialKeys.h>

#include <algorithm>

namespace cerfcore {

namespace host_platform {

namespace {

bool CalloutDevice(io_object_t service, std::string& path) {
    CFTypeRef value = ::IORegistryEntryCreateCFProperty(service, CFSTR(kIOCalloutDeviceKey),
                                                        kCFAllocatorDefault, 0);
    if (value == nullptr) return false;
    bool ok = false;
    if (::CFGetTypeID(value) == ::CFStringGetTypeID()) {
        const CFStringRef text = static_cast<CFStringRef>(value);
        const CFIndex     cap  = ::CFStringGetMaximumSizeForEncoding(::CFStringGetLength(text),
                                                                     kCFStringEncodingUTF8) + 1;
        std::string buf(static_cast<size_t>(cap), '\0');
        if (::CFStringGetCString(text, &buf[0], cap, kCFStringEncodingUTF8)) {
            buf.resize(std::char_traits<char>::length(buf.c_str()));
            path = buf;
            ok   = !path.empty();
        }
    }
    ::CFRelease(value);
    return ok;
}

}

std::vector<std::string> EnumerateSerialPorts() {
    std::vector<std::string> ports;
    CFMutableDictionaryRef   matching = ::IOServiceMatching(kIOSerialBSDServiceValue);
    if (matching == nullptr) return ports;
    io_iterator_t services = 0;
    if (::IOServiceGetMatchingServices(kIOMainPortDefault, matching, &services) != KERN_SUCCESS) {
        return ports;
    }
    while (const io_object_t service = ::IOIteratorNext(services)) {
        std::string path;
        if (CalloutDevice(service, path)) ports.push_back(path);
        ::IOObjectRelease(service);
    }
    ::IOObjectRelease(services);
    std::sort(ports.begin(), ports.end());
    return ports;
}

}

}
