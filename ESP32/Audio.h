// Part of SimCoupe - A SAM Coupe emulator
//
// Audio.h: ESP32 audio output (ES8311 via I2S)
//
// Replaces SDL/Audio.h for the ESP32 port.

#pragma once

class Audio
{
public:
    static bool Init();
    static void Exit();
    static float AddData(uint8_t* pData, int len_bytes);
};
