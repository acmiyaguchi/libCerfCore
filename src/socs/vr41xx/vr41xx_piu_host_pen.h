#pragma once

#include <cstdint>
#include <functional>
#include <optional>

class CerfEmulator;
namespace cerfengine { class HostRequestChannel; }

struct Vr41xxPiuPenPoint {
    bool     down;
    uint16_t x;
    uint16_t y;
};

class Vr41xxPiuHostPen {
public:
    explicit Vr41xxPiuHostPen(CerfEmulator& emu) : emu_(emu) {}

    void Attach(std::function<void()> apply);
    void Wake();

    void Pen(bool down, uint16_t x, uint16_t y);
    void Tap(uint16_t x, uint16_t y);

    std::optional<Vr41xxPiuPenPoint> TakePen();
    std::optional<Vr41xxPiuPenPoint> TakeTap();

    void Clear();

private:
    static std::optional<Vr41xxPiuPenPoint> Take(std::optional<Vr41xxPiuPenPoint>& slot);

    CerfEmulator&                    emu_;
    cerfengine::HostRequestChannel*  requests_ = nullptr;
    std::optional<Vr41xxPiuPenPoint> pen_;
    std::optional<Vr41xxPiuPenPoint> tap_;
};
