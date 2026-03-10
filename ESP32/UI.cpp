// Part of SimCoupe - A SAM Coupe emulator
//
// UI.cpp: ESP32 user interface
//
// On ESP32 there is no window manager, no SDL event loop, no file drop.
// UI::CheckEvents() just polls the keyboard (via Input::Update()) and
// returns true to keep the emulation running.
// UI::CreateVideo() is defined in Video.cpp.

#include "SimCoupe.h"
#include "UI.h"

#include "Actions.h"
#include "CPU.h"
#include "Frame.h"
#include "GUI.h"
#include "Input.h"
#include "Options.h"
#include "Sound.h"

#include "esp_system.h"

bool g_fActive = true;

bool UI::Init(bool /*fFirstInit_*/)
{
    return true;
}

void UI::Exit(bool /*fReInit_*/)
{
}

// CheckEvents is called once per emulated frame.
// We drain the keyboard queue via Input::Update() and return true to continue.
bool UI::CheckEvents()
{
    // If the GUI is active, poll input here (same as SDL port)
    if (GUI::IsActive())
        Input::Update();

    // On ESP32 there is no quit event — run forever
    return true;
}

void UI::ShowMessage(MsgType type, const std::string& str)
{
    // On ESP32 we can only log; the GUI MsgBox still works if GUI is active
    if (type == MsgType::Info)
        GUI::Start(new MsgBox(nullptr, str, "SimCoupe", mbInformation));
    else if (type == MsgType::Warning)
        GUI::Start(new MsgBox(nullptr, str, "SimCoupe", mbWarning));
    else
        GUI::Start(new MsgBox(nullptr, str, "SimCoupe", mbError));
}

bool UI::DoAction(Action action, bool pressed)
{
    if (pressed)
    {
        switch (action)
        {
        case Action::ExitApp:
            // On ESP32 we can't really exit — just reset
            esp_restart();
            break;
        default:
            return false;
        }
    }
    return false;
}
