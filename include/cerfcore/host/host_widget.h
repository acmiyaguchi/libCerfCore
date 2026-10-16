#pragma once

#include "widget_control.h"
#include "widget_icon.h"

#include <atomic>
#include <string>
#include <vector>

namespace cerfcore {

class StateWriter;
class StateReader;

/* HostWidget is Service-free: implementers already derive Service (a
   peripheral via Peripheral), so a Service base here would make emu_
   ambiguous in them. Implementers self-register with HostWidgetRegistry
   from OnReady. */

enum class WidgetGroup : int {
    Storage   = 0,
    Network,
    Pcmcia,
    Usb,
    Indicator,
    Power,
    Debug,
    /* Terminal range - pinned to the right of the bar (always shown) and to the
       top of the Actions menu. The capture lock takes the highest rank so it is
       always the very last; other input widgets sort ahead of it. */
    GuestAdditions = 900,  /* always third from the right / third in Actions */
    InputControl   = 1000, /* input-device widgets, e.g. the touch/pointer switch */
    InputCapture   = 1100, /* the capture lock - always rightmost / menu-top */
};

class WidgetSection {
public:
    virtual ~WidgetSection() = default;
    virtual std::string Label() const = 0;
    virtual std::vector<WidgetControl> BuildControls() { return {}; }
    virtual bool PollDirty() { return false; }
    virtual void SaveState(StateWriter&) const {}
    virtual void RestoreState(StateReader&) {}
};

class HostWidget {
public:
    virtual ~HostWidget() = default;

    virtual std::string WidgetName() const = 0;
    virtual WidgetGroup Group() const = 0;
    virtual std::string Tooltip() const { return WidgetName(); }

    virtual bool TryPrimaryAction() { return false; }
    virtual std::vector<WidgetControl> Controls() { return {}; }
    virtual bool ControlsAreOneAction() const { return false; }

    /* false => the icon is dimmed to read as a disabled peripheral. */
    virtual bool IsEnabled() const { return true; }

    virtual WidgetIcon Icon() const = 0;

    /* Hot-path safe: an implementer marks data activity from its read/write
       path. One relaxed atomic store, no lock. */
    void MarkRx() { rx_pending_.store(true, std::memory_order_relaxed); }
    void MarkTx() { tx_pending_.store(true, std::memory_order_relaxed); }

    bool TakeRx() { return rx_pending_.exchange(false, std::memory_order_relaxed); }
    bool TakeTx() { return tx_pending_.exchange(false, std::memory_order_relaxed); }

    /* Per-tick repaint check for indicators that change without RX/TX (e.g. an
       LED). Return true when the drawn appearance changed. UI-thread only. */
    virtual bool PollDirty() { return false; }

    virtual void SaveWidgetState(StateWriter&) const {}
    virtual void RestoreWidgetState(StateReader&) {}

private:
    std::atomic<bool> rx_pending_{false};
    std::atomic<bool> tx_pending_{false};
};

}
