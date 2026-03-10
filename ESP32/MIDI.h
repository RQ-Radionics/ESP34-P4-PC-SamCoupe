// Part of SimCoupe - A SAM Coupe emulator
//
// MIDI.h: ESP32 MIDI stub (no MIDI hardware on ESP32-P4-PC)
//
// Replaces SDL/MIDI.h for the ESP32 port.

#pragma once

#include "SAMIO.h"

class MidiDevice : public IoDevice
{
public:
    MidiDevice() = default;
    ~MidiDevice() = default;

public:
    uint8_t In(uint16_t wPort_) override { return 0xFF; }
    void Out(uint16_t wPort_, uint8_t bVal_) override { }

public:
    bool SetDevice(const std::string& dev_path) { return false; }

protected:
    uint8_t m_abIn[256]{};
    uint8_t m_abOut[256]{};
    int m_nIn = 0, m_nOut = 0;
    int m_nDevice = -1;
};

extern std::unique_ptr<MidiDevice> pMidi;
