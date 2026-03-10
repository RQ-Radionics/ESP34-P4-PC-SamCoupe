// Z80_JLS_SAM_stubs.h — force-included when compiling Z80_JLS.cpp for SimCoupe
//
// Provides stub implementations of all ESPectrum-specific types and functions
// that Z80_JLS.cpp references. These stubs redirect to SimCoupe equivalents
// or provide no-ops for functionality not needed on SAM Coupé.
//
// This file is force-included via -include compiler flag (see CMakeLists.txt).

#pragma once
#include <stdint.h>
#include <string>
#include <cstring>   // strcmp, strcpy, etc.
#include <cstdio>    // FILE, fopen, fseek, fscanf

using std::string;   // Z80_JLS.cpp uses bare 'string' without std::

// ── CPU::frame_cycles / CPU::stFrame / CPU::tstates ──────────────────────────
// SimCoupe's CPU namespace (defined in CPU.cpp)
namespace CPU
{
    extern uint32_t frame_cycles;
    extern uint32_t stFrame;   // dummy — set by HALT, not used by SimCoupe loop
    extern uint16_t last_in_port, last_out_port;
    extern uint8_t  last_in_val,  last_out_val;

    // Z80_JLS exec_nocheck() uses CPU::tstates — alias to frame_cycles
    static inline uint32_t& tstates_ref() { return frame_cycles; }
}
// Provide CPU::tstates as a macro so exec_nocheck() compiles
// (exec_nocheck is never called by SimCoupe but must compile)
#define tstates frame_cycles

// ── rtrim — used in decodeOpcodebf() tape flash-load path ────────────────────
// Trims trailing whitespace from a std::string in-place.
static inline void rtrim(string& s)
{
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                          s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
}

// ── VIDEO::Draw ───────────────────────────────────────────────────────────────
// Z80_JLS calls VIDEO::Draw(n, contended) for interrupt acknowledge (7 cycles)
// and NMI (1 cycle). We add those cycles to CPU::frame_cycles.
namespace VIDEO
{
    static inline void Draw(int32_t tstates, bool /*contended*/)
    {
        CPU::frame_cycles += static_cast<uint32_t>(tstates);
    }
    static inline void Draw_Opcode(bool /*contended*/) {}
}

// ── Ports::input / Ports::output ─────────────────────────────────────────────
// Z80_JLS calls these for Z80 IN/OUT instructions.
// BASE_ASIC_PORT: ports below this are non-ASIC (no contention)
#ifndef BASE_ASIC_PORT
#define BASE_ASIC_PORT 0xe0
#endif

namespace IO
{
    uint8_t In(uint16_t port);
    void    Out(uint16_t port, uint8_t val);

    // Inline implementation matching SimCoupe's IO::WaitStates in SAMIO.h.
    // SAM Coupé ASIC contention: 0-7 cycle delay aligned to 8-cycle boundary.
    static inline int WaitStates(uint32_t fc, uint16_t port)
    {
        if ((port & 0xff) < BASE_ASIC_PORT)
            return 0;
        constexpr auto mask = 7;
        return mask - static_cast<int>((fc + 2) & mask);
    }
}

namespace Ports
{
    static inline uint8_t input(uint16_t port)
    {
        CPU::frame_cycles += IO::WaitStates(CPU::frame_cycles, port);
        uint8_t val = IO::In(port);
        CPU::last_in_port = port;
        CPU::last_in_val  = val;
        return val;
    }
    static inline void output(uint16_t port, uint8_t val)
    {
        CPU::frame_cycles += IO::WaitStates(CPU::frame_cycles, port);
        CPU::last_out_port = port;
        CPU::last_out_val  = val;
        IO::Out(port, val);
    }
}

// ── MemESP — used only in check_trdos() and exec_nocheck() ───────────────────
// check_trdos() is a no-op for SAM Coupé; exec_nocheck() is never called.
namespace MemESP
{
    static bool     ramContended[4]  = { false, false, false, false };
    static uint8_t* ramCurrent[4]    = { nullptr, nullptr, nullptr, nullptr };
    static uint8_t* rom[8]           = {};
    static int      romInUse         = 0;
    static int      romLatch         = 0;
    static int      pagingmode2A3    = 0;
    static uint8_t  lastContendedMemReadWrite = 0;
}

// ── ESPectrum — used only in check_trdos() ───────────────────────────────────
namespace ESPectrum
{
    static bool trdos = false;
}

// ── Config — used only in check_trdos() and tape flash-load ──────────────────
namespace Config
{
    static int  DiskCtrl  = 0;
    static bool tapFPI    = false;
    static bool flashload = false;
    static int  lang      = 0;
}

// ── FileUtils — used only in check_trdos() ───────────────────────────────────
namespace FileUtils
{
    static std::string MountPoint = "";
}

// ── OSD — used only in check_trdos() ─────────────────────────────────────────
#define LEVEL_WARN 1
static const char* OSD_TAPE_SELECT_ERR[] = { "" };

namespace OSD
{
    static inline void osdCenteredMsg(const char* /*msg*/, int /*level*/) {}
}
