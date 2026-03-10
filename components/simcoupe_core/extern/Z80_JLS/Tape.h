// Stub: Tape.h — ESPectrum tape API stubs for Z80_JLS compilation
//
// Z80_JLS.cpp includes "Tape.h" and uses ESPectrum's Tape:: namespace
// (tapeFileType, tapeFileName, FlashLoad, etc.) inside check_trdos() and
// tape flash-load code. These code paths are never executed on SAM Coupé.
// This stub shadows SimCoupe's Base/Tape.h for Z80_JLS.cpp only.

#pragma once
#include <string>
#include <cstdio>   // FILE*

#define TAPE_FTYPE_TAP  0
#define TAPE_FTYPE_TZX  1
#define TAPE_LOADING    1
#define TAPE_PHASE_DATA 2

namespace Tape
{
    static int         tapeFileType  = -1;
    static std::string tapeFileName  = "none";
    static FILE*       tape          = nullptr;   // must be FILE* for fseek()
    static bool        tapeIsReadOnly = true;
    static int         tapeStatus    = 0;
    static int         tapeCurBlock  = 0;

    static inline bool FlashLoad() { return false; }
    static inline void Stop()      {}
    static inline void Play()      {}
    static inline void Save()      {}   // stub — tape save not used on SAM Coupé
}
