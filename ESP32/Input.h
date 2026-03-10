// Part of SimCoupe - A SAM Coupe emulator
//
// Input.h: ESP32 keyboard input (USB HID via sim_kbd)
//
// Replaces SDL/Input.h for the ESP32 port.

#pragma once

class Input
{
public:
    static bool Init();
    static void Exit();

    static void Update();

    static bool IsMouseAcquired() { return false; }
    static void AcquireMouse(bool fAcquire_ = true) { }
    static void Purge();

    static int MapChar(int nChar_, int* pnMods_ = nullptr);
    static int MapKey(int nKey_);
};
