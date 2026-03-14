///////////////////////////////////////////////////////////////////////////////
//
// z80cpp - Z80 emulator core
//
// Copyright (c) 2017, 2018, 2019, 2020 jsanchezv - https://github.com/jsanchezv
//
// Heretic optimizations and minor adaptations
// Copyright (c) 2021 dcrespo3d - https://github.com/dcrespo3d
//
// SimCoupe adaptation (removed ESPectrum-specific code, added SimCoupe hooks)
// Copyright (c) 2026 RQ-Radionics
//

#ifndef Z80OPERATIONS_H
#define Z80OPERATIONS_H

#include <stdint.h>

// Class grouping callbacks for Z80 operations.
// All function pointers must be assigned before Z80::execute() is called.

class Z80Ops
{
public:
    // Fetch opcode from RAM (increments R, applies contention)
    static uint8_t (*fetchOpcode)();

    // Read/write byte
    static uint8_t (*peek8)(uint16_t address);
    static void    (*poke8)(uint16_t address, uint8_t value);

    // Read/write word (two byte accesses, low byte first)
    static uint16_t (*peek16)(uint16_t address);
    static void     (*poke16)(uint16_t address, RegisterPair word);

    // Put an address on bus lasting 'tstates' cycles (contention / wait states)
    static void (*addressOnBus)(uint16_t address, int32_t wstates);

    // INT signal: return true when /INT is asserted
    static bool (*isActiveINT)();

    // I/O port read/write (used by IN/OUT and block I/O instructions)
    static uint8_t (*inPort)(uint16_t port);
    static void    (*outPort)(uint16_t port, uint8_t value);

    // ── SimCoupe-specific hooks (optional, nullptr = no-op) ──────────────────

    // Called when EI is executed (before pendingEI is set)
    static void (*notifyEI)();

    // Called on any RST nn (nn = 0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38)
    // after push(PC) and before PC is set. Return true to suppress the RST
    // (caller has already handled it, e.g. ROM hook).
    static bool (*notifyRST)(uint8_t target);

    // Called on unconditional RET (after PC is loaded from stack)
    static void (*notifyRET)();

    // Called on conditional RET cc when condition is true and Z flag is set
    // (i.e. RET Z taken). Return true to suppress the return (Tape hook).
    static bool (*notifyRETZ)();
};

#endif // Z80OPERATIONS_H
