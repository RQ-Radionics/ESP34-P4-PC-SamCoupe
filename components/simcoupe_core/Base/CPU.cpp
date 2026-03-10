 // Part of SimCoupe - A SAM Coupe emulator
//
// CPU.cpp: Z80 processor emulation and main emulation loop
//
//  Copyright (c) 1999-2015 Simon Owen
//  Copyright (c) 2000-2003 Dave Laundon
//  Copyright (c) 1996-2001 Allan Skillman
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

#include "SimCoupe.h"
#include "CPU.h"

#include "BlueAlpha.h"
#include "Debug.h"
#include "Events.h"
#include "Frame.h"
#include "GUI.h"
#include "Input.h"
#include "Keyin.h"
#include "SAMIO.h"
#include "Memory.h"
#include "Mouse.h"
#include "Options.h"
#include "Tape.h"
#include "UI.h"

#include "Z80_JLS/z80.h"
#include "Z80_JLS/z80operations.h"

// The global cpu wrapper object (thin shell around Z80:: statics)
sam_cpu cpu;

bool g_fBreak, g_fPaused;
int g_nTurbo;

constexpr auto max_boot_frames{ 200 };
int boot_frames;

#ifdef _DEBUG
bool debug_break;
#endif


namespace CPU
{
uint32_t frame_cycles;
bool reset_asserted = false;
uint16_t last_in_port, last_out_port;
uint8_t last_in_val, last_out_val;

// Dummy: set to 0 by Z80_JLS HALT handler (decodeOpcode76).
// SimCoupe's ExecuteChunk() checks Z80::isHalted() directly, so this is unused.
uint32_t stFrame = 0;

bool Init(bool fFirstInit_/*=false*/)
{
    bool fRet = true;

    if (fFirstInit_)
    {
        // Initialise Z80_JLS core
        Z80::create();

        // Set up Z80Ops function pointers (SAM Coupé has one memory model)
        Z80Ops::fetchOpcode  = &Z80Ops::fetchOpcode_std;
        Z80Ops::peek8        = &Z80Ops::peek8_std;
        Z80Ops::poke8        = &Z80Ops::poke8_std;
        Z80Ops::peek16       = &Z80Ops::peek16_std;
        Z80Ops::poke16       = &Z80Ops::poke16_std;
        Z80Ops::addressOnBus = &Z80Ops::addressOnBus_std;

        InitEvents();
        AddEvent(EventType::FrameInterrupt, 0);
        AddEvent(EventType::InputUpdate, CPU_CYCLES_PER_FRAME * 3 / 4);

        // Hard reset on first init
        Z80::reset();

        fRet &= Memory::Init(true) && IO::Init();
    }

    Reset(true);
    Reset(false);

    return fRet;
}

void Exit(bool fReInit_)
{
    IO::Exit(fReInit_);
    Memory::Exit(fReInit_);

    if (!fReInit_)
    {
        Breakpoint::RemoveAll();
        Z80::destroy();
    }
}


void ExecuteChunk()
{
    if (reset_asserted)
    {
        CPU::frame_cycles = CPU_CYCLES_PER_FRAME;
        CheckEvents(CPU::frame_cycles);
        return;
    }

    for (g_fBreak = false; !g_fBreak; )
    {
        // Execute one Z80 instruction.
        // All T-state counting is done in the Z80Ops callbacks (Z80Ops_SAM.cpp):
        //   fetchOpcode: 4 base + contention
        //   peek8:       3 base + contention
        //   poke8:       3 base + contention
        //   addressOnBus: extra cycles (displacement, internal ops)
        //   VIDEO::Draw: interrupt acknowledge (7) and NMI (1) cycles
        // CPU::frame_cycles is updated directly by those callbacks.
        Z80::execute();

        CheckEvents(CPU::frame_cycles);

        // Check for maskable interrupt (INT line asserted)
        if ((~IO::State().status & STATUS_INT_MASK) && Memory::full_contention)
            Z80::checkINT();

        // Z80_JLS handles DD/FD prefixes internally — no mid-prefix state
        // is exposed, so we can always check breakpoints here.

#ifdef _DEBUG
        if (debug_break)
        {
            Debug::Start();
            debug_break = false;
        }
#endif

        if (Breakpoint::breakpoints.empty())
            continue;

        Debug::AddTraceRecord();

        if (auto bp_index = Breakpoint::Hit())
        {
            CheckEvents(CPU::frame_cycles);
            Debug::Start(bp_index);
        }
    }

    if (boot_frames > 0 && !--boot_frames)
        g_nTurbo &= ~TURBO_BOOT;
}

void Run()
{
    while (UI::CheckEvents())
    {
        if (g_fPaused)
            continue;

        if (!Debug::IsActive() && !GUI::IsModal())
            ExecuteChunk();

        Frame::End();

        if (CPU::frame_cycles >= CPU_CYCLES_PER_FRAME)
        {
            EventFrameEnd(CPU_CYCLES_PER_FRAME);

            IO::FrameUpdate();
            Debug::FrameEnd();
            Frame::Flyback();

            CPU::frame_cycles %= CPU_CYCLES_PER_FRAME;
        }
    }

    TRACE("Quitting main emulation loop...\n");
}

void Reset(bool active)
{
    if (GetOption(fastreset) && reset_asserted && !active)
    {
        g_nTurbo |= TURBO_BOOT;
        boot_frames = max_boot_frames;
    }

    reset_asserted = active;
    if (reset_asserted)
    {
        Z80::reset();

        Keyin::Stop();
        Tape::Stop();

        IO::Init();
        Memory::Init();

        Debug::Refresh();
    }
}

void NMI()
{
    Z80::triggerNMI();
    Debug::Refresh();
}

} // namespace CPU
