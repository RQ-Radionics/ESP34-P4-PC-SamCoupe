// Part of SimCoupe - A SAM Coupe emulator
//
// Input.cpp: ESP32 keyboard input via USB HID (sim_kbd component)
//
// sim_kbd delivers PS/2 Set 2 scancode bytes via a FreeRTOS queue.
// We decode them here into HK_ host-key codes and call Keyboard::SetKey().
//
// PS/2 Set 2 protocol:
//   Normal make:    xx
//   Normal break:   F0 xx
//   Extended make:  E0 xx
//   Extended break: E0 F0 xx
//   Pause make:     E1 14 77 E1 F0 14 F0 77  (no break code)
//
// We maintain a small state machine to handle multi-byte sequences.

#include "SimCoupe.h"
#include "Input.h"
#include "Keyboard.h"
#include "Options.h"
#include "Actions.h"
#include "GUI.h"

#include "sim_kbd.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char* TAG = "input";

// Queue created by Input::Init(), passed to sim_kbd_init()
static QueueHandle_t s_kbd_queue = nullptr;

// PS/2 decoder state
enum class Ps2State { IDLE, BREAK, EXT, EXT_BREAK };
static Ps2State s_state = Ps2State::IDLE;

// Current modifier state (for passing to Keyboard::SetKey)
static int s_mods = HM_NONE;

////////////////////////////////////////////////////////////////////////////////
// PS/2 Set 2 scancode → HK_ host key mapping
// Returns HK_NONE if the scancode is not mapped.
////////////////////////////////////////////////////////////////////////////////

static int MapPs2Normal(uint8_t sc)
{
    switch (sc)
    {
    // Row 0 — function keys
    case 0x05: return HK_F1;
    case 0x06: return HK_F2;
    case 0x04: return HK_F3;
    case 0x0C: return HK_F4;
    case 0x03: return HK_F5;
    case 0x0B: return HK_F6;
    case 0x83: return HK_F7;
    case 0x0A: return HK_F8;
    case 0x01: return HK_F9;
    case 0x09: return HK_F10;
    case 0x78: return HK_F11;
    case 0x07: return HK_F12;

    // Digits row
    case 0x16: return '1';
    case 0x1E: return '2';
    case 0x26: return '3';
    case 0x25: return '4';
    case 0x2E: return '5';
    case 0x36: return '6';
    case 0x3D: return '7';
    case 0x3E: return '8';
    case 0x46: return '9';
    case 0x45: return '0';
    case 0x4E: return '-';
    case 0x55: return '=';
    case 0x66: return HK_BACKSPACE;

    // QWERTY row
    case 0x15: return 'q';
    case 0x1D: return 'w';
    case 0x24: return 'e';
    case 0x2D: return 'r';
    case 0x2C: return 't';
    case 0x35: return 'y';
    case 0x3C: return 'u';
    case 0x43: return 'i';
    case 0x44: return 'o';
    case 0x4D: return 'p';
    case 0x54: return '[';
    case 0x5B: return ']';
    case 0x5D: return '\\';

    // ASDF row
    case 0x1C: return 'a';
    case 0x1B: return 's';
    case 0x23: return 'd';
    case 0x2B: return 'f';
    case 0x34: return 'g';
    case 0x33: return 'h';
    case 0x3B: return 'j';
    case 0x42: return 'k';
    case 0x4B: return 'l';
    case 0x4C: return ';';
    case 0x52: return '\'';
    case 0x5A: return HK_RETURN;

    // ZXCV row
    case 0x1A: return 'z';
    case 0x22: return 'x';
    case 0x21: return 'c';
    case 0x2A: return 'v';
    case 0x32: return 'b';
    case 0x31: return 'n';
    case 0x3A: return 'm';
    case 0x41: return ',';
    case 0x49: return '.';
    case 0x4A: return '/';

    // Modifiers / special
    case 0x12: return HK_LSHIFT;
    case 0x59: return HK_RSHIFT;
    case 0x14: return HK_LCTRL;   // Left Ctrl (E0 14 = Right Ctrl, handled in MapPs2Ext)
    case 0x11: return HK_LALT;    // Left Alt  (E0 11 = Right Alt)
    case 0x0D: return HK_TAB;
    case 0x76: return HK_ESC;
    case 0x29: return HK_SPACE;
    case 0x58: return HK_CAPSLOCK;

    // Numpad
    case 0x70: return HK_KP0;
    case 0x69: return HK_KP1;
    case 0x72: return HK_KP2;
    case 0x7A: return HK_KP3;
    case 0x6B: return HK_KP4;
    case 0x73: return HK_KP5;
    case 0x74: return HK_KP6;
    case 0x6C: return HK_KP7;
    case 0x75: return HK_KP8;
    case 0x7D: return HK_KP9;
    case 0x79: return HK_KPPLUS;
    case 0x7B: return HK_KPMINUS;
    case 0x7C: return HK_KPMULT;
    case 0x71: return HK_KPDECIMAL;
    case 0x77: return HK_NUMLOCK;

    // Misc
    case 0x7E: return HK_SCROLL;
    case 0x84: return HK_PRINT;   // SysRq (non-extended)

    default:   return HK_NONE;
    }
}

static int MapPs2Extended(uint8_t sc)
{
    switch (sc)
    {
    case 0x75: return HK_UP;
    case 0x72: return HK_DOWN;
    case 0x6B: return HK_LEFT;
    case 0x74: return HK_RIGHT;

    case 0x70: return HK_INSERT;
    case 0x71: return HK_DELETE;
    case 0x6C: return HK_HOME;
    case 0x69: return HK_END;
    case 0x7D: return HK_PGUP;
    case 0x7A: return HK_PGDN;

    case 0x14: return HK_RCTRL;
    case 0x11: return HK_RALT;
    case 0x1F: return HK_LWIN;
    case 0x27: return HK_RWIN;
    case 0x2F: return HK_APPS;

    case 0x4A: return HK_KPDIVIDE;
    case 0x5A: return HK_KPENTER;

    case 0x7C: return HK_PRINT;   // E0 7C = PrintScreen make
    case 0x7E: return HK_PAUSE;   // E0 7E = Ctrl+Break

    default:   return HK_NONE;
    }
}

////////////////////////////////////////////////////////////////////////////////
// Update modifier state from key event
////////////////////////////////////////////////////////////////////////////////

static void UpdateMods(int hk, bool pressed)
{
    int bit = HM_NONE;
    switch (hk)
    {
    case HK_LSHIFT: bit = HM_LSHIFT; break;
    case HK_RSHIFT: bit = HM_RSHIFT; break;
    case HK_LCTRL:  bit = HM_LCTRL;  break;
    case HK_RCTRL:  bit = HM_RCTRL;  break;
    case HK_LALT:   bit = HM_LALT;   break;
    case HK_RALT:   bit = HM_RALT;   break;
    default: break;
    }
    if (pressed)
        s_mods |= bit;
    else
        s_mods &= ~bit;
}

////////////////////////////////////////////////////////////////////////////////
// Process one PS/2 scancode byte through the state machine
////////////////////////////////////////////////////////////////////////////////

// Send key event to GUI if active. Returns true if GUI consumed the event.
static bool SendToGui(int hk, bool pressed)
{
    if (!GUI::IsActive())
        return false;
    
    // Only send key press events to GUI (not releases)
    if (!pressed)
        return true;  // Consume release events when GUI is active
    
    // Send the key to GUI as GM_CHAR message
    // hk is the host key code (HK_LEFT, HK_RIGHT, HK_RETURN, 'a', etc.)
    GUI::SendMessage(GM_CHAR, hk, s_mods);
    return true;
}

static void ProcessScancode(uint8_t byte)
{
    switch (s_state)
    {
    case Ps2State::IDLE:
        if (byte == 0xE0)
        {
            s_state = Ps2State::EXT;
        }
        else if (byte == 0xF0)
        {
            s_state = Ps2State::BREAK;
        }
        else if (byte == 0xE1)
        {
            // Pause key — consume the whole 8-byte sequence by ignoring it
            // (no break code; we just skip)
        }
        else
        {
            int hk = MapPs2Normal(byte);
            if (hk != HK_NONE)
            {
                UpdateMods(hk, true);
                // If GUI is active, send to GUI instead of keyboard matrix
                if (SendToGui(hk, true))
                    break;
                Keyboard::SetKey(hk, true, s_mods, 0);
                // Trigger SimCoupe actions for F-keys (F1=Reset, F12=Insert Disk, etc.)
                if (hk >= HK_F1 && hk <= HK_F12)
                {
                    bool fCtrl = (s_mods & (HM_LCTRL | HM_RCTRL)) != 0;
                    bool fAlt  = (s_mods & (HM_LALT | HM_RALT)) != 0;
                    bool fShift = (s_mods & (HM_LSHIFT | HM_RSHIFT)) != 0;
                    Actions::Key(hk - HK_F1 + 1, true, fCtrl, fAlt, fShift);
                }
            }
        }
        break;

    case Ps2State::BREAK:
        s_state = Ps2State::IDLE;
        {
            int hk = MapPs2Normal(byte);
            if (hk != HK_NONE)
            {
                UpdateMods(hk, false);
                // If GUI is active, send release to GUI (will be consumed)
                if (SendToGui(hk, false))
                    break;
                Keyboard::SetKey(hk, false, s_mods, 0);
                // Trigger SimCoupe actions for F-keys (release)
                if (hk >= HK_F1 && hk <= HK_F12)
                {
                    bool fCtrl = (s_mods & (HM_LCTRL | HM_RCTRL)) != 0;
                    bool fAlt  = (s_mods & (HM_LALT | HM_RALT)) != 0;
                    bool fShift = (s_mods & (HM_LSHIFT | HM_RSHIFT)) != 0;
                    Actions::Key(hk - HK_F1 + 1, false, fCtrl, fAlt, fShift);
                }
            }
        }
        break;

    case Ps2State::EXT:
        if (byte == 0xF0)
        {
            s_state = Ps2State::EXT_BREAK;
        }
        else
        {
            s_state = Ps2State::IDLE;
            int hk = MapPs2Extended(byte);
            if (hk != HK_NONE)
            {
                UpdateMods(hk, true);
                // If GUI is active, send to GUI instead of keyboard matrix
                if (SendToGui(hk, true))
                    break;
                Keyboard::SetKey(hk, true, s_mods, 0);
            }
        }
        break;

    case Ps2State::EXT_BREAK:
        s_state = Ps2State::IDLE;
        {
            int hk = MapPs2Extended(byte);
            if (hk != HK_NONE)
            {
                UpdateMods(hk, false);
                // If GUI is active, send release to GUI (will be consumed)
                if (SendToGui(hk, false))
                    break;
                Keyboard::SetKey(hk, false, s_mods, 0);
            }
        }
        break;
    }
}

////////////////////////////////////////////////////////////////////////////////
// Input API
////////////////////////////////////////////////////////////////////////////////

bool Input::Init()
{
    Exit();

    s_state = Ps2State::IDLE;
    s_mods  = HM_NONE;

    // Create a queue for PS/2 scancode bytes (depth 64 bytes is plenty)
    s_kbd_queue = xQueueCreate(64, sizeof(uint8_t));
    if (!s_kbd_queue)
    {
        ESP_LOGE(TAG, "Failed to create keyboard queue");
        return false;
    }

    esp_err_t err = sim_kbd_init(s_kbd_queue);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "sim_kbd_init failed: 0x%x", err);
        vQueueDelete(s_kbd_queue);
        s_kbd_queue = nullptr;
        return false;
    }

    Keyboard::Init();
    return true;
}

void Input::Exit()
{
    if (s_kbd_queue)
    {
        sim_kbd_deinit();
        vQueueDelete(s_kbd_queue);
        s_kbd_queue = nullptr;
    }
}

// Called once per frame — drain all pending scancode bytes from the queue
void Input::Update()
{
    if (!s_kbd_queue)
        return;

    uint8_t byte;
    while (xQueueReceive(s_kbd_queue, &byte, 0) == pdTRUE)
        ProcessScancode(byte);

    Keyboard::Update();

    // CapsLock is a toggle key — release it every frame so SimCoupe sees
    // a clean press/release cycle (same approach as the SDL port)
    Keyboard::SetKey(HK_CAPSLOCK, false);
    Keyboard::SetKey(HK_NUMLOCK,  false);
}

void Input::Purge()
{
    if (s_kbd_queue)
        xQueueReset(s_kbd_queue);

    s_state = Ps2State::IDLE;
    s_mods  = HM_NONE;
    Keyboard::Purge();
}

int Input::MapChar(int nChar_, int* /*pnMods_*/)
{
    // Return the character/key code directly as the scancode index.
    // This covers:
    //   - ASCII HK_ values (HK_RETURN='\r'=13, HK_SPACE=32, etc.) which are < HK_MIN
    //   - Extended HK_ values (HK_LSHIFT=256, HK_F1, etc.) which are >= HK_MIN
    // Zero means "no mapping".
    if (nChar_ > 0 && nChar_ < HK_MAX)
        return nChar_;
    return 0;
}

int Input::MapKey(int nKey_)
{
    // On ESP32 we don't use this path (scancodes are decoded directly in
    // ProcessScancode), but it must exist for the linker.
    return (nKey_ && nKey_ < HK_MIN) ? nKey_ : HK_NONE;
}
