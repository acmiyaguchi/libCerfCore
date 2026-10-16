#pragma once

#include "../core/service.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

class HwScreen : public Service {
public:
    using Service::Service;

    void AddLine(std::string_view line);

    std::string LastLine() const;
    uint64_t    Snapshot(std::vector<std::string>& out) const;

private:
    static constexpr size_t kMaxLines = 500;

    mutable std::mutex      mtx_;
    std::deque<std::string> lines_;
    uint64_t                gen_ = 0;
};
