#include "host_app_bundle.h"

#include <cstring>

namespace cerfcore {

namespace host_platform {

size_t AppBundleLength(const char* executable_path, size_t length) {
    static constexpr char kInBundle[]      = ".app/Contents/MacOS/";
    constexpr size_t      kInBundleLength  = sizeof(kInBundle) - 1;
    constexpr size_t      kExtensionLength = 4;
    size_t                cut              = 0;
    for (size_t i = 0; i < length; ++i) {
        if (executable_path[i] == '/') cut = i + 1;
    }
    if (cut < kInBundleLength ||
        std::memcmp(executable_path + cut - kInBundleLength, kInBundle, kInBundleLength) != 0) {
        return 0;
    }
    return cut - kInBundleLength + kExtensionLength;
}

}

}
