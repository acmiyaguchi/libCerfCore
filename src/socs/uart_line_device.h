#pragma once

class StateWriter;
class StateReader;

class UartLineDevice {
public:
    virtual ~UartLineDevice() = default;

    virtual void SaveState(StateWriter&) {}
    virtual void RestoreState(StateReader&) {}
};
