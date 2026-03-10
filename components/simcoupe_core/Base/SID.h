// SID.h — ESP32 stub: SID/reSID disabled (SAM Coupé has no SID chip)
// Replaces the original reSID-based implementation.
#pragma once

#include "Sound.h"

// Stub for the reSID SID chip class (SIDDevice holds a unique_ptr<SID>)
class SID
{
public:
    SID() = default;
    ~SID() = default;
};

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
    int m_chip_type = 0;
};

extern std::unique_ptr<SIDDevice> pSID;
