#include <cerfcore/host/hw_screen.h>

#include <cerfcore/core/cerf_emulator.h>

REGISTER_SERVICE(HwScreen);

void HwScreen::AddLine(std::string_view line) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (lines_.size() >= kMaxLines) lines_.pop_front();
    lines_.emplace_back(line);
    ++gen_;
}

std::string HwScreen::LastLine() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return lines_.empty() ? std::string() : lines_.back();
}

uint64_t HwScreen::Snapshot(std::vector<std::string>& out) const {
    std::lock_guard<std::mutex> lk(mtx_);
    out.assign(lines_.begin(), lines_.end());
    return gen_;
}
