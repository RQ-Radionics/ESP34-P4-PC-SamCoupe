// Part of SimCoupe - A SAM Coupe emulator
//
// IDEDisk.h: ESP32 IDE disk stub (no raw IDE on ESP32)
//
// Replaces SDL/IDEDisk.h for the ESP32 port.

#pragma once

#include "HardDisk.h"

class DeviceHardDisk : public HardDisk
{
public:
    DeviceHardDisk(const std::string& disk_path) : HardDisk(disk_path) { }

public:
    bool IsOpen() const { return false; }
    bool Open(bool read_only = false) override { return false; }
    void Close() { }

    bool ReadSector(unsigned int uSector_, uint8_t* pb_) override { return false; }
    bool WriteSector(unsigned int uSector_, uint8_t* pb_) override { return false; }

protected:
    int m_hDevice = -1;
};
