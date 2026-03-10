// Part of SimCoupe - A SAM Coupe emulator
//
// OSD.h: ESP32-P4 "OS-dependent" functions
//
// Replaces SDL/OSD.h for the ESP32 port.
// The ESP32 runs FreeRTOS + newlib — no SDL, no X11, no desktop OS.

#pragma once

#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/ioctl.h>

#define PATH_SEPARATOR '/'

// strcasecmp is in strings.h on POSIX; newlib provides it
#include <strings.h>

////////////////////////////////////////////////////////////////////////////////

class OSD
{
public:
    static bool Init();
    static void Exit();

    static std::string MakeFilePath(PathType type, const std::string& filename="");
    static bool IsHidden(const std::string& path);
    static std::string GetClipboardText();
    static void SetClipboardText(const std::string& str);

    static void DebugTrace(const std::string& str);
};
