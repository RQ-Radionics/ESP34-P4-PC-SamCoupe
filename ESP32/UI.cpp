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
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "speed";

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

    // Log emulation speed every 30 seconds.
    // ACTUAL_FRAMES_PER_SECOND = 50 (PAL SAM Coupé).
    // We count frames here and compute % speed independently of Frame.cpp's
    // profile_text (which is an internal variable not exposed in Frame.h).
    static int64_t s_last_log_us = 0;
    static int     s_frame_count = 0;
    s_frame_count++;

    int64_t now_us = esp_timer_get_time();
    if (s_last_log_us == 0)
    {
        s_last_log_us = now_us;
    }
    else
    {
        int64_t elapsed_us = now_us - s_last_log_us;
        if (elapsed_us >= 30'000'000)  // 30 seconds
        {
            float fps     = s_frame_count * 1'000'000.0f / elapsed_us;
            float percent = fps / ACTUAL_FRAMES_PER_SECOND * 100.0f;
            ESP_LOGI(TAG, "Speed: %.0f%% (%.1f fps, %d frames in %.1fs)",
                     percent, fps, s_frame_count, elapsed_us / 1'000'000.0f);
            s_last_log_us = now_us;
            s_frame_count = 0;
        }
    }

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
