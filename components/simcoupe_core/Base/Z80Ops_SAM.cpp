// Z80Ops_SAM.cpp — Z80_JLS callbacks for SimCoupe
//
// Also provides the out-of-line definitions for Z80Ops static members.
//
// Defines all Z80Ops function pointer targets that connect the Z80_JLS
// core to SimCoupe's memory, I/O, timing, and hook systems.

#include "SimCoupe.h"
#include "CPU.h"
#include "Memory.h"
#include "SAMIO.h"
#include "Debug.h"
#include "Tape.h"

#include "Z80_JLS/z80operations.h"
#include "Z80_JLS/z80.h"

// ── Z80Ops static member definitions ─────────────────────────────────────────
// These must be defined in exactly one .cpp file.
uint8_t  (*Z80Ops::fetchOpcode)()                       = nullptr;
uint8_t  (*Z80Ops::peek8)(uint16_t)                     = nullptr;
void     (*Z80Ops::poke8)(uint16_t, uint8_t)            = nullptr;
uint16_t (*Z80Ops::peek16)(uint16_t)                    = nullptr;
void     (*Z80Ops::poke16)(uint16_t, RegisterPair)      = nullptr;
void     (*Z80Ops::addressOnBus)(uint16_t, int32_t)     = nullptr;
bool     (*Z80Ops::isActiveINT)()                       = nullptr;
uint8_t  (*Z80Ops::inPort)(uint16_t)                    = nullptr;
void     (*Z80Ops::outPort)(uint16_t, uint8_t)          = nullptr;
void     (*Z80Ops::notifyEI)()                          = nullptr;
bool     (*Z80Ops::notifyRST)(uint8_t)                  = nullptr;
void     (*Z80Ops::notifyRET)()                         = nullptr;
bool     (*Z80Ops::notifyRETZ)()                        = nullptr;

// ── Function pointer implementations ─────────────────────────────────────────

// fetchOpcode: read opcode byte, advance R, apply contention
static uint8_t sam_fetchOpcode()
{
    uint16_t pc = Z80::getRegPC();
    CPU::frame_cycles += Memory::WaitStates(CPU::frame_cycles, pc);
    return Memory::Read(pc);
}

static uint8_t sam_peek8(uint16_t address)
{
    CPU::frame_cycles += Memory::WaitStates(CPU::frame_cycles, address);
    return Memory::Read(address);
}

static void sam_poke8(uint16_t address, uint8_t value)
{
    CPU::frame_cycles += Memory::WaitStates(CPU::frame_cycles, address);
    Memory::Write(address, value);
}

static uint16_t sam_peek16(uint16_t address)
{
    uint8_t lo = sam_peek8(address);
    uint8_t hi = sam_peek8((address + 1) & 0xFFFF);
    return (uint16_t)(lo | (hi << 8));
}

static void sam_poke16(uint16_t address, RegisterPair word)
{
    sam_poke8(address,                   word.byte8.lo);
    sam_poke8((address + 1) & 0xFFFF,   word.byte8.hi);
}

static void sam_addressOnBus(uint16_t address, int32_t wstates)
{
    CPU::frame_cycles += Memory::WaitStates(CPU::frame_cycles, address) + wstates;
}

static bool sam_isActiveINT()
{
    return (~IO::State().status & STATUS_INT_MASK) && Memory::full_contention;
}

static uint8_t sam_inPort(uint16_t port)
{
    CPU::last_in_port = port;
    CPU::last_in_val  = IO::In(port);
    return CPU::last_in_val;
}

static void sam_outPort(uint16_t port, uint8_t value)
{
    CPU::last_out_port = port;
    CPU::last_out_val  = value;
    IO::Out(port, value);
}

// ── SimCoupe-specific hooks ───────────────────────────────────────────────────

static void sam_notifyEI()
{
    IO::EiHook();
}

static bool sam_notifyRST(uint8_t target)
{
    if (target == 0x30)
        IO::Rst48Hook();
    else if (target == 0x08)
        return IO::Rst8Hook();   // true = suppress RST (hook handled it)
    return false;
}

static void sam_notifyRET()
{
    Debug::OnRet();
}

static bool sam_notifyRETZ()
{
    Debug::RetZHook();
    return Tape::RetZHook();  // true = suppress RET Z (tape fast load)
}

// ── Init: assign all function pointers ───────────────────────────────────────

void Z80Ops_SAM_init()
{
    Z80Ops::fetchOpcode  = sam_fetchOpcode;
    Z80Ops::peek8        = sam_peek8;
    Z80Ops::poke8        = sam_poke8;
    Z80Ops::peek16       = sam_peek16;
    Z80Ops::poke16       = sam_poke16;
    Z80Ops::addressOnBus = sam_addressOnBus;
    Z80Ops::isActiveINT  = sam_isActiveINT;
    Z80Ops::inPort       = sam_inPort;
    Z80Ops::outPort      = sam_outPort;
    Z80Ops::notifyEI     = sam_notifyEI;
    Z80Ops::notifyRST    = sam_notifyRST;
    Z80Ops::notifyRET    = sam_notifyRET;
    Z80Ops::notifyRETZ   = sam_notifyRETZ;

    Z80::create();
}
