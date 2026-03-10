// Part of SimCoupe - A SAM Coupe emulator
//
// Options.cpp: Option saving, loading and command-line processing
//
//  Copyright (c) 1999-2014 Simon Owen
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

// Notes:
//  Options specified on the command-line override options in the file.
//  The settings are only and written back when it's closed.

#include "SimCoupe.h"
#include "Options.h"

#include "SAMIO.h"

namespace Options
{
Config g_config;


static void SetValue(int& value, const std::string& str)
{
    try {
        value = std::stoi(str);
    } catch (...) {
        // keep default value
    }
}

static void SetValue(bool& value, const std::string& str)
{
    value = str == "1" || tolower(str) == "yes";
}

static void SetValue(std::string& value, const std::string_view& sv)
{
    value = sv;
}

static bool SetNamedValue(const std::string& option_name, const std::string& str)
{
    auto name = trim(tolower(option_name));

    if (name == "cfgversion") SetValue(g_config.cfgversion, str);
    else if (name == "firstrun") SetValue(g_config.firstrun, str);
    else if (name == "windowpos") SetValue(g_config.windowpos, str);
    else if (name == "tvaspect") SetValue(g_config.tvaspect, str);
    else if (name == "fullscreen") SetValue(g_config.fullscreen, str);
    else if (name == "visiblearea") SetValue(g_config.visiblearea, str);
    else if (name == "smooth") SetValue(g_config.smooth, str);
    else if (name == "motionblur") SetValue(g_config.motionblur, str);
    else if (name == "allowmotionblur") SetValue(g_config.allowmotionblur, str);
    else if (name == "blurpercent") SetValue(g_config.blurpercent, str);
    else if (name == "maxintensity") SetValue(g_config.maxintensity, str);
    else if (name == "blackborder") SetValue(g_config.blackborder, str);
    else if (name == "tryvrr") SetValue(g_config.tryvrr, str);
    else if (name == "gifframeskip") SetValue(g_config.gifframeskip, str);
    else if (name == "rom") SetValue(g_config.rom, str);
    else if (name == "romwrite") SetValue(g_config.romwrite, str);
    else if (name == "atombootrom") SetValue(g_config.atombootrom, str);
    else if (name == "fastreset") SetValue(g_config.fastreset, str);
    else if (name == "asicdelay") SetValue(g_config.asicdelay, str);
    else if (name == "mainmem") SetValue(g_config.mainmem, str);
    else if (name == "externalmem") SetValue(g_config.externalmem, str);
    else if (name == "cmosz80") SetValue(g_config.cmosz80, str);
    else if (name == "im2random") SetValue(g_config.im2random, str);
    else if (name == "speed") SetValue(g_config.speed, str);
    else if (name == "drive1") SetValue(g_config.drive1, str);
    else if (name == "drive2") SetValue(g_config.drive2, str);
    else if (name == "turbodisk") SetValue(g_config.turbodisk, str);
    else if (name == "dosboot") SetValue(g_config.dosboot, str);
    else if (name == "dosdisk") SetValue(g_config.dosdisk, str);
    else if (name == "stdfloppy") SetValue(g_config.stdfloppy, str);
    else if (name == "nextfile") SetValue(g_config.nextfile, str);
    else if (name == "turbotape") SetValue(g_config.turbotape, str);
    else if (name == "tapetraps") SetValue(g_config.tapetraps, str);
    else if (name == "disk1") SetValue(g_config.disk1, str);
    else if (name == "disk2") SetValue(g_config.disk2, str);
    else if (name == "atomdisk0") SetValue(g_config.atomdisk0, str);
    else if (name == "atomdisk1") SetValue(g_config.atomdisk1, str);
    else if (name == "sdidedisk") SetValue(g_config.sdidedisk, str);
    else if (name == "tape") SetValue(g_config.tape, str);
    else if (name == "autoload") SetValue(g_config.autoload, str);
    else if (name == "autoboot") SetValue(g_config.autoboot, str);
    else if (name == "diskerrorfreq") SetValue(g_config.diskerrorfreq, str);
    else if (name == "samdiskhelper") SetValue(g_config.samdiskhelper, str);
    else if (name == "inpath") SetValue(g_config.inpath, str);
    else if (name == "outpath") SetValue(g_config.outpath, str);
    else if (name == "mru0") SetValue(g_config.mru0, str);
    else if (name == "mru1") SetValue(g_config.mru1, str);
    else if (name == "mru2") SetValue(g_config.mru2, str);
    else if (name == "mru3") SetValue(g_config.mru3, str);
    else if (name == "mru4") SetValue(g_config.mru4, str);
    else if (name == "mru5") SetValue(g_config.mru5, str);
    else if (name == "mru6") SetValue(g_config.mru6, str);
    else if (name == "mru7") SetValue(g_config.mru7, str);
    else if (name == "mru8") SetValue(g_config.mru8, str);
    else if (name == "keymapping") SetValue(g_config.keymapping, str);
    else if (name == "altforcntrl") SetValue(g_config.altforcntrl, str);
    else if (name == "altgrforedit") SetValue(g_config.altgrforedit, str);
    else if (name == "mouse") SetValue(g_config.mouse, str);
    else if (name == "mouseesc") SetValue(g_config.mouseesc, str);
    else if (name == "joydev1") SetValue(g_config.joydev1, str);
    else if (name == "joydev2") SetValue(g_config.joydev2, str);
    else if (name == "joytype1") SetValue(g_config.joytype1, str);
    else if (name == "joytype2") SetValue(g_config.joytype2, str);
    else if (name == "deadzone1") SetValue(g_config.deadzone1, str);
    else if (name == "deadzone2") SetValue(g_config.deadzone2, str);
    else if (name == "parallel1") SetValue(g_config.parallel1, str);
    else if (name == "parallel2") SetValue(g_config.parallel2, str);
    else if (name == "printeronline") SetValue(g_config.printeronline, str);
    else if (name == "flushdelay") SetValue(g_config.flushdelay, str);
    else if (name == "midi") SetValue(g_config.midi, str);
    else if (name == "midiindev") SetValue(g_config.midiindev, str);
    else if (name == "midioutdev") SetValue(g_config.midioutdev, str);
    else if (name == "sambusclock") SetValue(g_config.sambusclock, str);
    else if (name == "dallasclock") SetValue(g_config.dallasclock, str);
    else if (name == "audiosync") SetValue(g_config.audiosync, str);
    else if (name == "saahighpass") SetValue(g_config.saahighpass, str);
    else if (name == "latency") SetValue(g_config.latency, str);
    else if (name == "dac7c") SetValue(g_config.dac7c, str);
    else if (name == "samplerfreq") SetValue(g_config.samplerfreq, str);
    else if (name == "voicebox") SetValue(g_config.voicebox, str);
    else if (name == "sid") SetValue(g_config.sid, str);
    else if (name == "drivelights") SetValue(g_config.drivelights, str);
    else if (name == "profile") SetValue(g_config.profile, str);
    else if (name == "status") SetValue(g_config.status, str);
    else if (name == "breakonexec") SetValue(g_config.breakonexec, str);
    else if (name == "fkeys") SetValue(g_config.fkeys, str);
    else if (name == "rasterdebug") SetValue(g_config.rasterdebug, str);
    else
    {
        return false;
    }

    return true;
}

bool Load(int argc_, char* argv_[])
{
    // Set defaults.
    g_config = {};

    auto path = OSD::MakeFilePath(PathType::Settings, OPTIONS_FILE);
    if (FILE* file = fopen(path.c_str(), "r"))
    {
        char line[512];
        while (fgets(line, sizeof(line), file))
        {
            // Strip trailing \r\n
            size_t len = strlen(line);
            while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n'))
                line[--len] = '\0';

            // Split on first '='
            char* eq = strchr(line, '=');
            if (eq)
            {
                *eq = '\0';
                std::string key(line);
                std::string value(eq + 1);
                if (!SetNamedValue(key, value))
                {
                    TRACE("Unknown setting: {}={}", key, value);
                }
            }
        }
        fclose(file);
    }

    // If the loaded configuration is incompatible, reset to defaults.
    if (g_config.cfgversion != ConfigVersion)
        g_config = {};

    auto drive_arg = 1;
    g_config.autoboot = true;

    while (argc_ && --argc_)
    {
        auto pcszOption = *++argv_;
        if (*pcszOption == '-')
        {
            pcszOption++;
            argc_--;

            if (argc_ <= 0 || !SetNamedValue(pcszOption, *++argv_))
            {
                TRACE("Unknown command-line option: {}\n", pcszOption);
            }
        }
        else
        {
            auto full_path{ fs::absolute(pcszOption) };

            // Bare filenames will be inserted into drive 1 then 2
            switch (drive_arg++)
            {
            case 1:
                SetOption(disk1, full_path.string());
                SetOption(drive1, drvFloppy);
                break;

            case 2:
                SetOption(disk2, full_path.string());
                SetOption(drive2, drvFloppy);
                break;

            default:
                TRACE("Unexpected command-line parameter: {}\n", pcszOption);
                break;
            }
        }
    }

    if (drive_arg > 1)
        IO::QueueAutoBoot(AutoLoadType::Disk);

    return true;
}

bool Save()
{
    auto path = OSD::MakeFilePath(PathType::Settings, OPTIONS_FILE);
    FILE* ofs = fopen(path.c_str(), "w");
    if (!ofs)
    {
        TRACE("Failed to save options\n");
        return false;
    }

    // Helper lambda: write key=value\n using fprintf
    auto W = [&](const char* key, const std::string& val) {
        fprintf(ofs, "%s=%s\n", key, val.c_str());
    };
    auto WI = [&](const char* key, int val) {
        fprintf(ofs, "%s=%d\n", key, val);
    };
    auto WB = [&](const char* key, bool val) {
        fprintf(ofs, "%s=%d\n", key, val ? 1 : 0);
    };

    using namespace std;
    WI("cfgversion",    g_config.cfgversion);
    WB("firstrun",      g_config.firstrun);
    W ("windowpos",     g_config.windowpos);
    WB("tvaspect",      g_config.tvaspect);
    WB("fullscreen",    g_config.fullscreen);
    WI("visiblearea",   g_config.visiblearea);
    WB("smooth",        g_config.smooth);
    WB("motionblur",    g_config.motionblur);
    WB("allowmotionblur", g_config.allowmotionblur);
    WI("blurpercent",   g_config.blurpercent);
    WI("maxintensity",  g_config.maxintensity);
    WB("blackborder",   g_config.blackborder);
    WB("tryvrr",        g_config.tryvrr);
    WI("gifframeskip",  g_config.gifframeskip);
    W ("rom",           g_config.rom);
    WB("romwrite",      g_config.romwrite);
    WB("atombootrom",   g_config.atombootrom);
    WB("fastreset",     g_config.fastreset);
    WB("asicdelay",     g_config.asicdelay);
    WI("mainmem",       g_config.mainmem);
    WI("externalmem",   g_config.externalmem);
    WB("cmosz80",       g_config.cmosz80);
    WB("im2random",     g_config.im2random);
    WI("speed",         g_config.speed);
    WI("drive1",        g_config.drive1);
    WI("drive2",        g_config.drive2);
    WB("turbodisk",     g_config.turbodisk);
    WB("dosboot",       g_config.dosboot);
    W ("dosdisk",       g_config.dosdisk);
    WB("stdfloppy",     g_config.stdfloppy);
    WB("nextfile",      g_config.nextfile);
    WB("turbotape",     g_config.turbotape);
    WB("tapetraps",     g_config.tapetraps);
    W ("disk1",         g_config.disk1);
    W ("disk2",         g_config.disk2);
    W ("atomdisk0",     g_config.atomdisk0);
    W ("atomdisk1",     g_config.atomdisk1);
    W ("sdidedisk",     g_config.sdidedisk);
    W ("tape",          g_config.tape);
    WB("autoload",      g_config.autoload);
    WI("diskerrorfreq", g_config.diskerrorfreq);
    WB("samdiskhelper", g_config.samdiskhelper);
    W ("inpath",        g_config.inpath);
    W ("outpath",       g_config.outpath);
    W ("mru0",          g_config.mru0);
    W ("mru1",          g_config.mru1);
    W ("mru2",          g_config.mru2);
    W ("mru3",          g_config.mru3);
    W ("mru4",          g_config.mru4);
    W ("mru5",          g_config.mru5);
    W ("mru6",          g_config.mru6);
    W ("mru7",          g_config.mru7);
    W ("mru8",          g_config.mru8);
    WI("keymapping",    g_config.keymapping);
    WB("altforcntrl",   g_config.altforcntrl);
    WB("altgrforedit",  g_config.altgrforedit);
    WB("mouse",         g_config.mouse);
    WB("mouseesc",      g_config.mouseesc);
    W ("joydev1",       g_config.joydev1);
    W ("joydev2",       g_config.joydev2);
    WI("joytype1",      g_config.joytype1);
    WI("joytype2",      g_config.joytype2);
    WI("deadzone1",     g_config.deadzone1);
    WI("deadzone2",     g_config.deadzone2);
    WI("parallel1",     g_config.parallel1);
    WI("parallel2",     g_config.parallel2);
    WB("printeronline", g_config.printeronline);
    WI("flushdelay",    g_config.flushdelay);
    WI("midi",          g_config.midi);
    W ("midiindev",     g_config.midiindev);
    W ("midioutdev",    g_config.midioutdev);
    WB("sambusclock",   g_config.sambusclock);
    WB("dallasclock",   g_config.dallasclock);
    WB("audiosync",     g_config.audiosync);
    WB("saahighpass",   g_config.saahighpass);
    WI("latency",       g_config.latency);
    WB("dac7c",         g_config.dac7c);
    WI("samplerfreq",   g_config.samplerfreq);
    WB("voicebox",      g_config.voicebox);
    WB("sid",           g_config.sid);
    WI("drivelights",   g_config.drivelights);
    WB("profile",       g_config.profile);
    WB("status",        g_config.status);
    WB("breakonexec",   g_config.breakonexec);
    W ("fkeys",         g_config.fkeys);
    WB("rasterdebug",   g_config.rasterdebug);

    fclose(ofs);

    return true;
}

} // namespace Options
