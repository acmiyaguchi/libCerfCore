#include <cerfcore/core/device_meta_label.h>

#include <cctype>
#include <regex>

#include <cerfcore/core/device_config.h>

namespace {

/* True when os_name already contains the {major}.{minor} version as a
   standalone token (launcher device_tree.py _os_name_has_version). std::regex
   (ECMAScript) has no lookbehind, so the leading "not a digit/dot" boundary is
   checked by hand on each match position; the trailing guard stays in-regex. */
bool OsNameHasVersion(const std::string& name, int major, int minor) {
    if (name.empty() || !(major || minor)) return false;

    auto preceded_ok = [&](const std::smatch& m) {
        size_t pos = static_cast<size_t>(m.position(0));
        if (pos == 0) return true;
        char c = name[pos - 1];
        return c != '.' && !std::isdigit(static_cast<unsigned char>(c));
    };

    {
        std::regex re(std::to_string(major) + "\\." + std::to_string(minor) +
                      "(?:\\.\\d+)*(?!\\d)");
        for (auto it = std::sregex_iterator(name.begin(), name.end(), re);
             it != std::sregex_iterator(); ++it) {
            if (preceded_ok(*it)) return true;
        }
    }
    if (minor == 0) {
        std::regex re(std::to_string(major) + "(?![\\d.])");
        for (auto it = std::sregex_iterator(name.begin(), name.end(), re);
             it != std::sregex_iterator(); ++it) {
            if (preceded_ok(*it)) return true;
        }
    }
    return false;
}

}  /* namespace */

std::string OsDisplayLabel(const DeviceMeta& meta) {
    std::string name = meta.os_name;
    size_t b = name.find_first_not_of(" \t");
    size_t e = name.find_last_not_of(" \t");
    name = (b == std::string::npos) ? std::string() : name.substr(b, e - b + 1);

    const int major = meta.os_ver_major, minor = meta.os_ver_minor;
    if (!(major || minor)) return name;
    if (OsNameHasVersion(name, major, minor)) return name;

    std::string tag = "(CE " + std::to_string(major) + "." +
                      std::to_string(minor) + ")";
    return name.empty() ? tag : (name + " " + tag);
}
