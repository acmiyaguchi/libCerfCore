#include <cerfcore/host/keyboard_router.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_input.h>

namespace cerfcore {

REGISTER_SERVICE(KeyboardRouter);

void KeyboardRouter::SelectAutoLocked() {
    KeyboardInput* best_ready = nullptr;
    KeyboardInput* best_any   = nullptr;
    for (auto* s : sources_) {
        if (!best_any || s->SourcePriority() > best_any->SourcePriority())
            best_any = s;
        if (!s->SourceReady()) continue;
        if (!best_ready || s->SourcePriority() > best_ready->SourcePriority())
            best_ready = s;
    }
    active_ = best_ready ? best_ready : best_any;
}

void KeyboardRouter::Register(KeyboardInput* src) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto* s : sources_) if (s == src) return;
    sources_.push_back(src);
    if (!user_picked_) SelectAutoLocked();
}

void KeyboardRouter::OnHostKey(uint8_t vk, bool key_up) {
    KeyboardInput* a;
    { std::lock_guard<std::mutex> lk(mtx_); a = active_; }
    if (a) a->OnHostKey(vk, key_up);
}

std::vector<KeyboardInput*> KeyboardRouter::Sources() {
    std::lock_guard<std::mutex> lk(mtx_);
    return sources_;
}

KeyboardInput* KeyboardRouter::Active() {
    std::lock_guard<std::mutex> lk(mtx_);
    return active_;
}

void KeyboardRouter::SetActive(KeyboardInput* src) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto* s : sources_)
        if (s == src) { active_ = src; user_picked_ = true; return; }
}

void KeyboardRouter::RestoreActiveByName(const std::string& name) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto* s : sources_)
        if (s->SourceName() == name) { active_ = s; return; }
}

void KeyboardRouter::ReevaluateAuto() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!user_picked_) SelectAutoLocked();
}

bool KeyboardRouter::UserPicked() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return user_picked_;
}

void KeyboardRouter::RestoreUserPicked(bool picked) {
    std::lock_guard<std::mutex> lk(mtx_);
    user_picked_ = picked;
}

void KeyboardRouter::RearmAutoSelect() {
    std::lock_guard<std::mutex> lk(mtx_);
    user_picked_ = false;
    SelectAutoLocked();
}

}
