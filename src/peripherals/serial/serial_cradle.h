#pragma once

#include <cerfcore/host/host_widget.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cerfcore {

class CerfEmulator;
class SerialLine;
class SerialEndpoint;
class StateWriter;
class StateReader;

class SerialCradle : public HostWidget {
public:
    SerialCradle(CerfEmulator& emu, SerialLine& line, std::string label);
    ~SerialCradle();

    void OnShutdown();

    void SaveCradleState(StateWriter& w);
    void RestoreCradleState(StateReader& r);
    void PostRestore();

    std::string WidgetName() const override { return label_; }
    WidgetGroup Group() const override { return WidgetGroup::Network; }
    std::string Tooltip() const override;
    std::vector<WidgetControl> Controls() override;
    WidgetIcon  Icon() const override;
    bool PollDirty() override;

private:
    enum class Kind { None, Modem, Forward };

    static constexpr const char* kModemId   = "modem";
    static constexpr const char* kForwardId = "forward";

    void SwapModem(uint64_t gen);
    void SwapForward(uint64_t gen, std::string host_port);
    void Eject(uint64_t gen);
    void SetPluggedLocked(Kind kind, std::string host_port);
    std::vector<SlotEntry> AvailableLocked(uint64_t gen);
    const char* IconResourceLocked() const;

    CerfEmulator& emu_;
    SerialLine&   line_;
    std::string   label_;

    mutable std::mutex mtx_;
    Kind                            kind_ = Kind::None;
    std::string                     host_port_;
    std::unique_ptr<SerialEndpoint> endpoint_;
    uint64_t                        generation_ = 0;

    Kind        restored_kind_ = Kind::None;
    std::string restored_port_;

    std::string ui_last_res_;
};

}
