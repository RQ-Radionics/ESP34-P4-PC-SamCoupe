// SID.h — stub: SID/reSID disabled on ESP32 (SAM Coupé has no SID chip)
#pragma once

#include "Sound.h"

// Stub for the reSID SID class (SIDDevice holds a unique_ptr<SID>)
class SID
{
public:
    SID() = default;
    ~SID() = default;
};

// Minimal stub so SAMIO.cpp compiles without reSID
class SIDDevice final : public SoundDevice
{
public:
    SIDDevice() = default;
    ~SIDDevice() override = default;

    void Reset() override {}
    void Update(bool /*fFrameEnd_*/ = false) {}
    void FrameEnd() override {}
    void Out(uint16_t /*wPort_*/, uint8_t /*bVal_*/) override {}

private:
    std::unique_ptr<SID> m_sid;  // kept to match original layout; unused
};

extern std::unique_ptr<SIDDevice> pSID;
