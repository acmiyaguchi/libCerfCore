#pragma once

#include "../core/service.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace cerfcore {

class KeyboardInput;

class KeyboardRouter : public Service {
public:
    using Service::Service;

    void Register(KeyboardInput* src);
    void OnHostKey(uint8_t vk, bool key_up);

    std::vector<KeyboardInput*> Sources();
    KeyboardInput*              Active();
    void                        SetActive(KeyboardInput* src);
    void                        RestoreActiveByName(const std::string& name);

    void ReevaluateAuto();
    void RearmAutoSelect();

    bool UserPicked() const;
    void RestoreUserPicked(bool picked);

private:
    void SelectAutoLocked();

    mutable std::mutex          mtx_;
    std::vector<KeyboardInput*> sources_;
    KeyboardInput*              active_ = nullptr;
    bool                        user_picked_ = false;
};

}
