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

// Initialise Z80Ops function pointers (defined in Z80Ops_SAM.cpp)
void Z80Ops_SAM_init();

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

// Z80 register accessors (thin wrappers over Z80_JLS statics)
inline uint16_t get_pc()   { return Z80::getRegPC(); }
inline uint16_t get_sp()   { return Z80::getRegSP(); }
inline uint16_t get_af()   { return Z80::getRegAF(); }
inline uint16_t get_bc()   { return Z80::getRegBC(); }
inline uint16_t get_de()   { return Z80::getRegDE(); }
inline uint16_t get_hl()   { return Z80::getRegHL(); }
inline uint16_t get_ix()   { return Z80::getRegIX(); }
inline uint16_t get_iy()   { return Z80::getRegIY(); }
inline bool     is_halted(){ return Z80::isHalted(); }
}

extern bool g_fBreak, g_fPaused;
extern int g_nTurbo;

#ifdef _DEBUG
extern bool debug_break;
#endif

// ── Compatibility shim: cpu object for Debug.cpp / Breakpoint.cpp ─────────────
// Provides the same interface as the old kosarev sam_cpu, delegating to Z80_JLS.

struct sam_cpu_shim
{
    // z80_state: flat snapshot of all Z80 registers (replaces kosarev's z80_state)
    struct z80_state
    {
        uint16_t pc{}, sp{}, af{}, bc{}, de{}, hl{};
        uint16_t ix{}, iy{};
        uint8_t  i{}, r{};
        uint16_t alt_af{}, alt_bc{}, alt_de{}, alt_hl{};
        bool iff1{}, iff2{};
        int int_mode{};
        bool halted{};

        // Flag convenience accessors (same as kosarev)
        uint16_t get_af() const { return af; }
        uint8_t  get_f()  const { return af & 0xFF; }
        uint8_t  get_a()  const { return af >> 8; }
        uint16_t get_bc() const { return bc; }
        uint16_t get_de() const { return de; }
        uint16_t get_hl() const { return hl; }
        uint16_t get_ix() const { return ix; }
        uint16_t get_iy() const { return iy; }
        uint16_t get_sp() const { return sp; }
        uint16_t get_pc() const { return pc; }
        uint8_t  get_i()  const { return i;  }
        uint8_t  get_r()  const { return r;  }
        bool get_iff1()   const { return iff1; }
        bool get_iff2()   const { return iff2; }
        int  get_int_mode() const { return int_mode; }
        uint16_t get_alt_af() const { return alt_af; }
        uint16_t get_alt_bc() const { return alt_bc; }
        uint16_t get_alt_de() const { return alt_de; }
        uint16_t get_alt_hl() const { return alt_hl; }
        bool is_halted()  const { return halted; }

        // on_mreq_wait stub (used in Debug.cpp for single-step timing display)
        void on_mreq_wait(uint16_t /*addr*/) const {}
    };

    // Allow Debug.cpp to do: sCurrRegs = cpu; (capture current state)
    operator z80_state() const {
        z80_state s;
        s.pc = Z80::getRegPC();  s.sp = Z80::getRegSP();
        s.af = Z80::getRegAF();  s.bc = Z80::getRegBC();
        s.de = Z80::getRegDE();  s.hl = Z80::getRegHL();
        s.ix = Z80::getRegIX();  s.iy = Z80::getRegIY();
        s.i  = Z80::getRegI();   s.r  = Z80::getRegR();
        s.alt_af = Z80::getRegAFx(); s.alt_bc = Z80::getRegBCx();
        s.alt_de = Z80::getRegDEx(); s.alt_hl = Z80::getRegHLx();
        s.iff1 = Z80::isIFF1();  s.iff2 = Z80::isIFF2();
        s.int_mode = (int)Z80::getIM();
        s.halted = Z80::isHalted();
        return s;
    }


    // on_mreq_wait stub (called from Debug.cpp single-step timing)
    void on_mreq_wait(uint16_t /*addr*/) const {}

    // Flag masks (kosarev naming)
    static constexpr uint8_t sf_mask = 0x80;
    static constexpr uint8_t zf_mask = 0x40;
    static constexpr uint8_t yf_mask = 0x20;
    static constexpr uint8_t hf_mask = 0x10;
    static constexpr uint8_t xf_mask = 0x08;
    static constexpr uint8_t pf_mask = 0x04;
    static constexpr uint8_t nf_mask = 0x02;
    static constexpr uint8_t cf_mask = 0x01;

    // Register getters — 16-bit pairs
    uint16_t get_pc()  const { return Z80::getRegPC(); }
    uint16_t get_sp()  const { return Z80::getRegSP(); }
    uint16_t get_af()  const { return Z80::getRegAF(); }
    uint16_t get_bc()  const { return Z80::getRegBC(); }
    uint16_t get_de()  const { return Z80::getRegDE(); }
    uint16_t get_hl()  const { return Z80::getRegHL(); }
    uint16_t get_ix()  const { return Z80::getRegIX(); }
    uint16_t get_iy()  const { return Z80::getRegIY(); }

    // Register getters — 8-bit
    uint8_t get_a()    const { return Z80::getRegA();        }
    uint8_t get_f()    const { return Z80::getFlags();       }
    uint8_t get_b()    const { return Z80::getRegB();        }
    uint8_t get_c()    const { return Z80::getRegC();        }
    uint8_t get_d()    const { return Z80::getRegD();        }
    uint8_t get_e()    const { return Z80::getRegE();        }
    uint8_t get_h()    const { return Z80::getRegH();        }
    uint8_t get_l()    const { return Z80::getRegL();        }
    uint8_t get_ixh()  const { return Z80::getRegIX() >> 8; }
    uint8_t get_ixl()  const { return Z80::getRegIX() & 0xFF; }
    uint8_t get_iyh()  const { return Z80::getRegIY() >> 8; }
    uint8_t get_iyl()  const { return Z80::getRegIY() & 0xFF; }
    uint8_t get_i()    const { return Z80::getRegI();        }
    uint8_t get_r()    const { return Z80::getRegR();        }

    // Interrupt / mode
    bool get_iff1()     const { return Z80::isIFF1();         }
    bool get_iff2()     const { return Z80::isIFF2();         }
    int  get_int_mode() const { return (int)Z80::getIM();     }

    // Alternate register set
    uint16_t get_alt_af() const { return Z80::getRegAFx(); }
    uint16_t get_alt_bc() const { return Z80::getRegBCx(); }
    uint16_t get_alt_de() const { return Z80::getRegDEx(); }
    uint16_t get_alt_hl() const { return Z80::getRegHLx(); }

    // Register setters
    void set_pc(uint16_t v)     { Z80::setRegPC(v); }
    void set_sp(uint16_t v)     { Z80::setRegSP(v); }
    void set_a(uint8_t v)       { Z80::setRegA(v);  }
    void set_f(uint8_t v)       { Z80::setFlags(v); }
    void set_b(uint8_t v)       { Z80::setRegB(v);  }
    void set_c(uint8_t v)       { Z80::setRegC(v);  }
    void set_d(uint8_t v)       { Z80::setRegD(v);  }
    void set_e(uint8_t v)       { Z80::setRegE(v);  }
    void set_h(uint8_t v)       { Z80::setRegH(v);  }
    void set_l(uint8_t v)       { Z80::setRegL(v);  }
    void set_bc(uint16_t v)     { Z80::setRegBC(v); }
    void set_de(uint16_t v)     { Z80::setRegDE(v); }
    void set_hl(uint16_t v)     { Z80::setRegHL(v); }
    void set_ix(uint16_t v)     { Z80::setRegIX(v); }
    void set_iy(uint16_t v)     { Z80::setRegIY(v); }
    void set_i(uint8_t v)       { Z80::setRegI(v);  }
    void set_r(uint8_t v)       { Z80::setRegR(v);  }
    void set_iff1(bool v)       { Z80::setIFF1(v);  }
    void set_iff2(bool v)       { Z80::setIFF2(v);  }
    void set_int_mode(int v)    { Z80::setIM((Z80::IntMode)v); }
    void set_is_halted(bool v)  { Z80::setHalted(v); }

    void set_af(uint16_t v)     { Z80::setRegAF(v); }
    void set_ixh(uint8_t v)     { Z80::setRegIX((Z80::getRegIX() & 0x00FF) | (v << 8)); }
    void set_ixl(uint8_t v)     { Z80::setRegIX((Z80::getRegIX() & 0xFF00) | v); }
    void set_iyh(uint8_t v)     { Z80::setRegIY((Z80::getRegIY() & 0x00FF) | (v << 8)); }
    void set_iyl(uint8_t v)     { Z80::setRegIY((Z80::getRegIY() & 0xFF00) | v); }
    void set_alt_af(uint16_t v) { Z80::setRegAFx(v); }
    void set_alt_bc(uint16_t v) { Z80::setRegBCx(v); }
    void set_alt_de(uint16_t v) { Z80::setRegDEx(v); }
    void set_alt_hl(uint16_t v) { Z80::setRegHLx(v); }

    bool is_halted() const      { return Z80::isHalted(); }

    // Exchange operations (kosarev triggers state change)
    void exx_regs() {
        // EXX: swap BC/DE/HL with alternates
        uint16_t bc = Z80::getRegBC(), de = Z80::getRegDE(), hl = Z80::getRegHL();
        Z80::setRegBC(Z80::getRegBCx()); Z80::setRegBCx(bc);
        Z80::setRegDE(Z80::getRegDEx()); Z80::setRegDEx(de);
        Z80::setRegHL(Z80::getRegHLx()); Z80::setRegHLx(hl);
    }
    void on_ex_de_hl_regs() {
        uint16_t de = Z80::getRegDE();
        Z80::setRegDE(Z80::getRegHL());
        Z80::setRegHL(de);
    }
};

extern sam_cpu_shim cpu;

// Alias for backward compatibility with Debug.cpp / Breakpoint.cpp
using sam_cpu = sam_cpu_shim;
