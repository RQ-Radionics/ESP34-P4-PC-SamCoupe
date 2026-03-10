// Part of SimCoupe - A SAM Coupe emulator
//
// Z80Ops_SAM.cpp: Z80_JLS callback implementations for SimCoupe
//
// Maps Z80_JLS memory/IO/contention callbacks to SimCoupe's Memory and IO subsystems.
//
// Cycle counting:
//   fetchOpcode: 4 base T-states + contention
//   peek8:       3 base T-states + contention
//   poke8:       3 base T-states + contention
//   peek16:      6 base T-states + contention (two peek8 calls)
//   poke16:      6 base T-states + contention (two poke8 calls)
//   addressOnBus: wstates extra T-states (for internal ops, displacement fetch, etc.)
//
// In ESPectrum, VIDEO::Draw(n, contended) adds n cycles to CPU::tstates.
// For SimCoupe we add cycles directly to CPU::frame_cycles.

#include "SimCoupe.h"
#include "CPU.h"
#include "Memory.h"
#include "SAMIO.h"

#include "Z80_JLS/z80.h"
#include "Z80_JLS/z80operations.h"

// ── Static function pointer definitions ──────────────────────────────────────

uint8_t (*Z80Ops::fetchOpcode)()                          = &Z80Ops::fetchOpcode_std;
uint8_t (*Z80Ops::peek8)(uint16_t address)                = &Z80Ops::peek8_std;
void    (*Z80Ops::poke8)(uint16_t address, uint8_t value) = &Z80Ops::poke8_std;
uint16_t(*Z80Ops::peek16)(uint16_t address)               = &Z80Ops::peek16_std;
void    (*Z80Ops::poke16)(uint16_t address, RegisterPair word) = &Z80Ops::poke16_std;
void    (*Z80Ops::addressOnBus)(uint16_t address, int32_t wstates) = &Z80Ops::addressOnBus_std;

bool Z80Ops::is48      = false;
bool Z80Ops::is128     = false;
bool Z80Ops::isPentagon = false;
bool Z80Ops::is2a3     = false;

// ── fetchOpcode ───────────────────────────────────────────────────────────────
//
// Called by Z80_JLS to fetch the opcode at PC (before PC is incremented).
// Adds 4 base T-states + memory contention for the opcode fetch M1 cycle.

uint8_t Z80Ops::fetchOpcode_std()
{
    uint16_t pc = Z80::getRegPC();
    // 4 base cycles for M1 opcode fetch + contention
    CPU::frame_cycles += 4 + Memory::WaitStates(CPU::frame_cycles, pc);
    return Memory::Read(pc);
}

uint8_t Z80Ops::fetchOpcode_2A3()
{
    return Z80Ops::fetchOpcode_std();
}

// ── peek8 ─────────────────────────────────────────────────────────────────────
//
// Called for memory reads (non-opcode). Adds 3 base T-states + contention.

uint8_t Z80Ops::peek8_std(uint16_t address)
{
    CPU::frame_cycles += 3 + Memory::WaitStates(CPU::frame_cycles, address);
    return Memory::Read(address);
}

uint8_t Z80Ops::peek8_2A3(uint16_t address)
{
    return Z80Ops::peek8_std(address);
}

// ── poke8 ─────────────────────────────────────────────────────────────────────
//
// Called for memory writes. Adds 3 base T-states + contention.

void Z80Ops::poke8_std(uint16_t address, uint8_t value)
{
    CPU::frame_cycles += 3 + Memory::WaitStates(CPU::frame_cycles, address);
    Memory::Write(address, value);
}

void Z80Ops::poke8_2A3(uint16_t address, uint8_t value)
{
    Z80Ops::poke8_std(address, value);
}

// ── peek16 ────────────────────────────────────────────────────────────────────
//
// Two consecutive peek8 calls (6 base T-states + contention for each byte).

uint16_t Z80Ops::peek16_std(uint16_t address)
{
    uint8_t lo = Z80Ops::peek8(address);
    uint8_t hi = Z80Ops::peek8(address + 1);
    return static_cast<uint16_t>((hi << 8) | lo);
}

uint16_t Z80Ops::peek16_2A3(uint16_t address)
{
    return Z80Ops::peek16_std(address);
}

// ── poke16 ────────────────────────────────────────────────────────────────────

void Z80Ops::poke16_std(uint16_t address, RegisterPair word)
{
    Z80Ops::poke8(address,     word.byte8.lo);
    Z80Ops::poke8(address + 1, word.byte8.hi);
}

void Z80Ops::poke16_2A3(uint16_t address, RegisterPair word)
{
    Z80Ops::poke16_std(address, word);
}

// ── addressOnBus ──────────────────────────────────────────────────────────────
//
// Called for extra bus cycles (displacement fetch, internal ops, etc.).
// wstates is the number of extra T-states to add.
// For SAM Coupé we add contention for each extra cycle.

void Z80Ops::addressOnBus_std(uint16_t address, int32_t wstates)
{
    for (int32_t i = 0; i < wstates; i++)
        CPU::frame_cycles += 1 + Memory::WaitStates(CPU::frame_cycles, address);
}

void Z80Ops::addressOnBus_2A3(uint16_t address, int32_t wstates)
{
    Z80Ops::addressOnBus_std(address, wstates);
}

// ── isActiveINT ───────────────────────────────────────────────────────────────
//
// Returns true when the /INT line is asserted.
// SAM Coupé: interrupt is active when STATUS_INT_MASK bits are clear.

bool Z80Ops::isActiveINT(void)
{
    return (~IO::State().status & STATUS_INT_MASK) != 0;
}
