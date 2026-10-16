#pragma once

#include <mutex>

namespace log_detail {

inline std::mutex& LogMutex() {
    static std::mutex* m = new std::mutex;
    return *m;
}

}
