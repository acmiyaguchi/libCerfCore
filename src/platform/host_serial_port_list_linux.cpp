#include "host_serial_port_list.h"

#include <algorithm>
#include <dirent.h>
#include <fstream>
#include <linux/serial.h>
#include <sstream>

namespace cerfcore {

namespace host_platform {

namespace {

bool PortHasNoUart(const std::string& name) {
    std::ifstream type("/sys/class/tty/" + name + "/type");
    int value = -1;
    return (type >> value) && value == PORT_UNKNOWN;
}

}

std::vector<std::string> EnumerateSerialPorts() {
    std::vector<std::string> prefixes;
    std::ifstream            drivers("/proc/tty/drivers");
    std::string              line;
    while (std::getline(drivers, line)) {
        std::istringstream fields(line);
        std::string        driver, device, major, minors, type;
        if (!(fields >> driver >> device >> major >> minors >> type)) continue;
        if (type != "serial" || device.rfind("/dev/", 0) != 0) continue;
        prefixes.push_back(device.substr(5));
    }
    std::vector<std::string> ports;
    DIR* dev = ::opendir("/dev");
    if (dev == nullptr) return ports;
    while (const dirent* entry = ::readdir(dev)) {
        const std::string name = entry->d_name;
        const bool matches = std::any_of(prefixes.begin(), prefixes.end(), [&](const std::string& p) {
            return name.size() > p.size() && name.compare(0, p.size(), p) == 0;
        });
        if (matches && !PortHasNoUart(name)) ports.push_back("/dev/" + name);
    }
    ::closedir(dev);
    std::sort(ports.begin(), ports.end());
    return ports;
}

}

}
