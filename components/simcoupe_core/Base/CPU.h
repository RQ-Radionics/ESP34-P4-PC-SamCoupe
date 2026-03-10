// Part of SimCoupe - A SAM Coupe emulator
//
// CPU.h: Z80 processor emulation and main emulation loop
//
//  Copyright (c) 2000-2003 Dave Laundon
//  Copyright (c) 1999-2014 Simon Owen
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

#pragma once

#include "Debug.h"
#include "Memory.h"
#include "Options.h"
#include "SAM.h"
#include "SAMIO.h"
#include "Tape.h"

#include "Z80_JLS/z80.h"
#include "Z80_JLS/z80operations.h"

// ── Compatibility shim: z80::get_high8 / get_low8 / make16 ──────────────────
// These were provided by the kosarev z80.h header and are used throughout
// Debug.cpp, Expr.cpp, and Tape.cpp.
namespace z80
{
    using fast_u8  = uint8_t;
    using fast_u16 = uint16_t;

    static inline constexpr fast_u8 get_low8(fast_u16 n)
    {
        return static_cast<fast_u8>(n & 0xff);
    }
    static inline constexpr fast_u8 get_high8(fast_u16 n)
    {
        return static_cast<fast_u8>(n >> 8);
    }
    static inline constexpr fast_u16 make16(fast_u8 hi, fast_u8 lo)
    {
        return static_cast<fast_u16>((hi << 8) | lo);
    }
} // namespace z80

// ── CPU constants ─────────────────────────────────────────────────────────────

constexpr uint16_t IM1_INTERRUPT_HANDLER = 0x0038;
constexpr uint16_t NMI_INTERRUPT_HANDLER = 0x0066;

constexpr uint8_t OP_NOP  = 0x00;
constexpr uint8_t OP_DJNZ = 0x10;
constexpr uint8_t OP_JR   = 0x18;
constexpr uint8_t OP_HALT = 0x76;
constexpr uint8_t OP_JP   = 0xc3;
constexpr uint8_t OP_RET  = 0xc9;
constexpr uint8_t OP_CALL = 0xcd;
constexpr uint8_t OP_JPHL = 0xe9;
constexpr uint8_t OP_DI   = 0xf3;

constexpr uint8_t IX_PREFIX = 0xdd;
constexpr uint8_t IY_PREFIX = 0xfd;
constexpr uint8_t CB_PREFIX = 0xcb;
constexpr uint8_t ED_PREFIX = 0xed;

// ── CPU namespace ─────────────────────────────────────────────────────────────

namespace CPU
{
bool Init(bool fFirstInit_ = false);
void Exit(bool fReInit_ = false);

void Run();
void ExecuteChunk();

void Reset(bool active);
void NMI();

extern uint32_t frame_cycles;
extern bool reset_asserted;
extern uint16_t last_in_port, last_out_port;
extern uint8_t last_in_val, last_out_val;
} // namespace CPU

extern bool g_fBreak, g_fPaused;
extern int g_nTurbo;

#ifdef _DEBUG
extern bool debug_break;
#endif

// ── sam_cpu: thin wrapper around Z80_JLS static state ────────────────────────
//
// Exposes the same get_*/set_* API that Debug.cpp, Expr.cpp, Tape.cpp, and
// Breakpoint.cpp use, but delegates to Z80:: static methods.
//
// z80_state is a plain-struct snapshot of all registers, also with get_* methods
// so that Debug.cpp can compare sLastRegs.get_pc() etc.

struct sam_cpu
{
    // ── Flag masks (same bit positions as Z80 F register) ────────────────────
    static constexpr uint8_t cf_mask = 0x01; // Carry
    static constexpr uint8_t nf_mask = 0x02; // Add/Sub
    static constexpr uint8_t pf_mask = 0x04; // Parity/Overflow
    static constexpr uint8_t xf_mask = 0x08; // Undocumented bit 3
    static constexpr uint8_t hf_mask = 0x10; // Half-carry
    static constexpr uint8_t yf_mask = 0x20; // Undocumented bit 5
    static constexpr uint8_t zf_mask = 0x40; // Zero
    static constexpr uint8_t sf_mask = 0x80; // Sign

    // ── z80_state: register snapshot ─────────────────────────────────────────
    struct z80_state
    {
        uint8_t  a, f;
        uint16_t bc, de, hl;
        uint16_t alt_af, alt_bc, alt_de, alt_hl;
        uint16_t ix, iy, sp, pc;
        uint8_t  i, r;
        bool     iff1, iff2;
        uint8_t  int_mode;
        bool     halted;

        // Accessors matching the sam_cpu API used in Debug.cpp macros
        uint8_t  get_a()      const { return a; }
        uint8_t  get_f()      const { return f; }
        uint16_t get_af()     const { return z80::make16(a, f); }
        uint8_t  get_b()      const { return z80::get_high8(bc); }
        uint8_t  get_c()      const { return z80::get_low8(bc); }
        uint16_t get_bc()     const { return bc; }
        uint8_t  get_d()      const { return z80::get_high8(de); }
        uint8_t  get_e()      const { return z80::get_low8(de); }
        uint16_t get_de()     const { return de; }
        uint8_t  get_h()      const { return z80::get_high8(hl); }
        uint8_t  get_l()      const { return z80::get_low8(hl); }
        uint16_t get_hl()     const { return hl; }
        uint16_t get_alt_af() const { return alt_af; }
        uint16_t get_alt_bc() const { return alt_bc; }
        uint16_t get_alt_de() const { return alt_de; }
        uint16_t get_alt_hl() const { return alt_hl; }
        uint8_t  get_ixh()    const { return z80::get_high8(ix); }
        uint8_t  get_ixl()    const { return z80::get_low8(ix); }
        uint16_t get_ix()     const { return ix; }
        uint8_t  get_iyh()    const { return z80::get_high8(iy); }
        uint8_t  get_iyl()    const { return z80::get_low8(iy); }
        uint16_t get_iy()     const { return iy; }
        uint16_t get_sp()     const { return sp; }
        uint16_t get_pc()     const { return pc; }
        uint8_t  get_i()      const { return i; }
        uint8_t  get_r()      const { return r; }
        bool     get_iff1()   const { return iff1; }
        bool     get_iff2()   const { return iff2; }
        uint8_t  get_int_mode() const { return int_mode; }
        bool     is_halted()  const { return halted; }
    };

    // ── Snapshot assignment: capture current Z80 state ───────────────────────
    // Allows: sLastRegs = cpu;  and  p->regs = cpu;
    operator z80_state() const
    {
        z80_state s;
        s.a        = Z80::getRegA();
        s.f        = Z80::getFlags();
        s.bc       = Z80::getRegBC();
        s.de       = Z80::getRegDE();
        s.hl       = Z80::getRegHL();
        s.alt_af   = Z80::getRegAFx();
        s.alt_bc   = Z80::getRegBCx();
        s.alt_de   = Z80::getRegDEx();
        s.alt_hl   = Z80::getRegHLx();
        s.ix       = Z80::getRegIX();
        s.iy       = Z80::getRegIY();
        s.sp       = Z80::getRegSP();
        s.pc       = Z80::getRegPC();
        s.i        = Z80::getRegI();
        s.r        = Z80::getRegR();
        s.iff1     = Z80::isIFF1();
        s.iff2     = Z80::isIFF2();
        s.int_mode = static_cast<uint8_t>(Z80::getIM());
        s.halted   = Z80::isHalted();
        return s;
    }

    // ── 8-bit register accessors ──────────────────────────────────────────────
    uint8_t get_a() const { return Z80::getRegA(); }
    void    set_a(uint8_t v) { Z80::setRegA(v); }

    uint8_t get_f() const { return Z80::getFlags(); }
    void    set_f(uint8_t v) { Z80::setFlags(v); }

    uint8_t get_b() const { return Z80::getRegB(); }
    void    set_b(uint8_t v) { Z80::setRegB(v); }

    uint8_t get_c() const { return Z80::getRegC(); }
    void    set_c(uint8_t v) { Z80::setRegC(v); }

    uint8_t get_d() const { return Z80::getRegD(); }
    void    set_d(uint8_t v) { Z80::setRegD(v); }

    uint8_t get_e() const { return Z80::getRegE(); }
    void    set_e(uint8_t v) { Z80::setRegE(v); }

    uint8_t get_h() const { return Z80::getRegH(); }
    void    set_h(uint8_t v) { Z80::setRegH(v); }

    uint8_t get_l() const { return Z80::getRegL(); }
    void    set_l(uint8_t v) { Z80::setRegL(v); }

    uint8_t get_i() const { return Z80::getRegI(); }
    void    set_i(uint8_t v) { Z80::setRegI(v); }

    uint8_t get_r() const { return Z80::getRegR(); }
    void    set_r(uint8_t v) { Z80::setRegR(v); }

    uint8_t get_ixh() const { return Z80::getRegIX() >> 8; }
    void    set_ixh(uint8_t v) { Z80::setRegIX((Z80::getRegIX() & 0x00ff) | (v << 8)); }

    uint8_t get_ixl() const { return Z80::getRegIX() & 0xff; }
    void    set_ixl(uint8_t v) { Z80::setRegIX((Z80::getRegIX() & 0xff00) | v); }

    uint8_t get_iyh() const { return Z80::getRegIY() >> 8; }
    void    set_iyh(uint8_t v) { Z80::setRegIY((Z80::getRegIY() & 0x00ff) | (v << 8)); }

    uint8_t get_iyl() const { return Z80::getRegIY() & 0xff; }
    void    set_iyl(uint8_t v) { Z80::setRegIY((Z80::getRegIY() & 0xff00) | v); }

    // ── 16-bit register accessors ─────────────────────────────────────────────
    uint16_t get_af() const { return Z80::getRegAF(); }
    void     set_af(uint16_t v) { Z80::setRegAF(v); }

    uint16_t get_bc() const { return Z80::getRegBC(); }
    void     set_bc(uint16_t v) { Z80::setRegBC(v); }

    uint16_t get_de() const { return Z80::getRegDE(); }
    void     set_de(uint16_t v) { Z80::setRegDE(v); }

    uint16_t get_hl() const { return Z80::getRegHL(); }
    void     set_hl(uint16_t v) { Z80::setRegHL(v); }

    uint16_t get_alt_af() const { return Z80::getRegAFx(); }
    void     set_alt_af(uint16_t v) { Z80::setRegAFx(v); }

    uint16_t get_alt_bc() const { return Z80::getRegBCx(); }
    void     set_alt_bc(uint16_t v) { Z80::setRegBCx(v); }

    uint16_t get_alt_de() const { return Z80::getRegDEx(); }
    void     set_alt_de(uint16_t v) { Z80::setRegDEx(v); }

    uint16_t get_alt_hl() const { return Z80::getRegHLx(); }
    void     set_alt_hl(uint16_t v) { Z80::setRegHLx(v); }

    uint16_t get_ix() const { return Z80::getRegIX(); }
    void     set_ix(uint16_t v) { Z80::setRegIX(v); }

    uint16_t get_iy() const { return Z80::getRegIY(); }
    void     set_iy(uint16_t v) { Z80::setRegIY(v); }

    uint16_t get_sp() const { return Z80::getRegSP(); }
    void     set_sp(uint16_t v) { Z80::setRegSP(v); }

    uint16_t get_pc() const { return Z80::getRegPC(); }
    void     set_pc(uint16_t v) { Z80::setRegPC(v); }

    // ── Interrupt / mode accessors ────────────────────────────────────────────
    bool get_iff1() const { return Z80::isIFF1(); }
    void set_iff1(bool v) { Z80::setIFF1(v); }

    bool get_iff2() const { return Z80::isIFF2(); }
    void set_iff2(bool v) { Z80::setIFF2(v); }

    uint8_t get_int_mode() const { return static_cast<uint8_t>(Z80::getIM()); }
    void    set_int_mode(uint8_t m)
    {
        switch (m)
        {
        case 0: Z80::setIM(Z80::IntMode::IM0); break;
        case 1: Z80::setIM(Z80::IntMode::IM1); break;
        case 2: Z80::setIM(Z80::IntMode::IM2); break;
        default: break;
        }
    }

    bool is_halted() const { return Z80::isHalted(); }
    void set_is_halted(bool v) { Z80::setHalted(v); }

    // ── EXX / EX DE,HL ───────────────────────────────────────────────────────
    void exx_regs()
    {
        uint16_t tmp;
        tmp = Z80::getRegBC(); Z80::setRegBC(Z80::getRegBCx()); Z80::setRegBCx(tmp);
        tmp = Z80::getRegDE(); Z80::setRegDE(Z80::getRegDEx()); Z80::setRegDEx(tmp);
        tmp = Z80::getRegHL(); Z80::setRegHL(Z80::getRegHLx()); Z80::setRegHLx(tmp);
    }

    void on_ex_de_hl_regs()
    {
        uint16_t tmp = Z80::getRegDE();
        Z80::setRegDE(Z80::getRegHL());
        Z80::setRegHL(tmp);
    }

    // ── on_mreq_wait: apply memory contention for a given address ────────────
    // Used by Debug.cpp cmdStepOver to remove opcode-fetch slack.
    void on_mreq_wait(uint16_t addr)
    {
        CPU::frame_cycles += Memory::WaitStates(CPU::frame_cycles, addr);
    }
};

extern sam_cpu cpu;
