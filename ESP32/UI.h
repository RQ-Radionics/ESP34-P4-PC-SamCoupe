// Part of SimCoupe - A SAM Coupe emulator
//
// UI.h: ESP32 user interface
//
// Replaces SDL/UI.h for the ESP32 port.

#pragma once

#include "Actions.h"
#include "Video.h"

class UI
{
public:
    static bool Init(bool fFirstInit_ = false);
    static void Exit(bool fReInit_ = false);

    static std::unique_ptr<IVideoBase> CreateVideo();
    static bool CheckEvents();

    static bool DoAction(Action action, bool pressed = true);
    static void ShowMessage(MsgType type, const std::string& str);
};

extern bool g_fActive;
